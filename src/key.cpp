/**************************************************************************
 * Copyright 2017 ArcMist, LLC                                            *
 * Contributors :                                                         *
 *   Curtis Ellis <curtis@arcmist.com>                                    *
 * Distributed under the MIT software license, see the accompanying       *
 * file license.txt or http://www.opensource.org/licenses/mit-license.php *
 **************************************************************************/
#include "key.hpp"

#ifdef PROFILER_ON
#include "arcmist/dev/profiler.hpp"
#endif

#include "arcmist/base/log.hpp"
#include "arcmist/base/math.hpp"
#include "arcmist/io/buffer.hpp"
#include "arcmist/crypto/digest.hpp"
#include "interpreter.hpp"

#define BITCOIN_KEY_LOG_NAME "BitCoin Key"


namespace BitCoin
{
    secp256k1_context *Key::sContext = NULL;

    secp256k1_context *Key::context()
    {
        if(sContext == NULL)
        {
            sContext = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
            std::atexit(destroyContext);
        }

        return sContext;
    }

    void Key::destroyContext()
    {
        secp256k1_context_destroy(sContext);
        sContext = NULL;
    }

    PrivateKey::PrivateKey()
    {
        // Create context with sign ability
        mContext = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
        std::memset(mData, 0, 32);
    }

    PrivateKey::~PrivateKey()
    {
        secp256k1_context_destroy(mContext);
    }

    bool PrivateKey::generate()
    {
        bool valid;

        uint32_t random;
        for(unsigned int i=0;i<32;i+=4)
        {
            random = ArcMist::Math::randomInt();
            std::memcpy(mData + i, &random, 4);
        }

        valid = secp256k1_ec_seckey_verify(mContext, mData);

        if(!valid)
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to generate private key");

        return valid;
    }

    ArcMist::String PrivateKey::hex() const
    {
        ArcMist::String result;
        result.writeHex(mData, 32);
        return result;
    }

    bool PrivateKey::generatePublicKey(PublicKey &pPublicKey) const
    {
        secp256k1_pubkey pubkey;
        if(!secp256k1_ec_pubkey_create(mContext, &pubkey, mData))
        {
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to generate public key");
            return false;
        }

        pPublicKey.set(pubkey.data);
        return true;
    }

    bool PrivateKey::sign(Hash &pHash, Signature &pSignature) const
    {
        if(pHash.size() != 32)
        {
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Wrong size hash to verify");
            return false;
        }

#ifdef PROFILER_ON
        ArcMist::Profiler profiler("Private Key Sign");
#endif
        secp256k1_ecdsa_signature signature;
        if(!secp256k1_ecdsa_sign(mContext, &signature, pHash.value(), mData,
          secp256k1_nonce_function_default, NULL))
        {
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to sign hash");
            return false;
        }

        pSignature.set(signature.data);
        return true;
    }

    bool Signature::verify(const PublicKey &pPublicKey, const Hash &pHash) const
    {
        if(!pPublicKey.isValid())
        {
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Invalid public key. Can't verify.");
            return false;
        }

        if(pHash.size() != 32)
        {
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Wrong size hash to verify");
            return false;
        }

#ifdef PROFILER_ON
        ArcMist::Profiler profiler("Signature Verify");
#endif
        if(secp256k1_ecdsa_verify(mContext, (const secp256k1_ecdsa_signature *)mData,
          pHash.value(), (const secp256k1_pubkey *)pPublicKey.value()))
            return true;

        if(!secp256k1_ecdsa_signature_normalize(mContext, (secp256k1_ecdsa_signature *)mData,
          (const secp256k1_ecdsa_signature *)mData))
            return false; // Already normalized

        // Try it again with the normalized signature
        if(secp256k1_ecdsa_verify(mContext, (const secp256k1_ecdsa_signature *)mData,
          pHash.value(), (const secp256k1_pubkey *)pPublicKey.value()))
            return true;

        return false;
    }

    ArcMist::String Signature::hex() const
    {
        ArcMist::String result;
        result.writeHex(mData, 64);
        return result;
    }

    ArcMist::String PublicKey::hex() const
    {
        ArcMist::String result;
        result.writeHex(mData, 64);
        return result;
    }

    void PublicKey::write(ArcMist::OutputStream *pStream, bool pCompressed, bool pScriptFormat) const
    {
        if(pCompressed)
        {
            size_t compressedLength = 33;
            uint8_t compressedData[compressedLength];
            if(!secp256k1_ec_pubkey_serialize(mContext, compressedData, &compressedLength, (const secp256k1_pubkey *)mData, SECP256K1_EC_COMPRESSED))
                ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to write compressed public key");
            else
            {
                if(pScriptFormat)
                    ScriptInterpreter::writePushDataSize(compressedLength, pStream);
                pStream->write(compressedData, compressedLength);
            }
        }
        else
        {
            size_t length = 65;
            uint8_t data[length];
            if(!secp256k1_ec_pubkey_serialize(mContext, data, &length, (const secp256k1_pubkey *)mData, 0))
                ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to write public key");
            else
            {
                if(pScriptFormat)
                    ScriptInterpreter::writePushDataSize(length, pStream);
                pStream->write(data, length);
            }
        }
    }

    bool PublicKey::read(ArcMist::InputStream *pStream)
    {
        size_t length;
        uint8_t *data;
        mValid = false;

        if(pStream->remaining() < 1)
            return false;

        // Check first byte to determine length
        uint8_t type = pStream->readByte();
        if(type == 0x02 || type == 0x03) // Compressed
            length = 33;
        else if(type == 0x04) // Uncompressed
            length = 65;
        else // Unknown
        {
            length = pStream->remaining() + 1;
            ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
              "Public key type unknown. type %02x size %d", type, length);
        }

        if(pStream->remaining() < length - 1)
        {
            ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
              "Failed to read public key. type %02x size %d", type, pStream->remaining() + 1);
            return false;
        }

        data = new uint8_t[length];
        data[0] = type;
        pStream->read(data + 1, length - 1);

#ifdef PROFILER_ON
        ArcMist::Profiler profiler("Public Key Read");
#endif
        if(secp256k1_ec_pubkey_parse(mContext, (secp256k1_pubkey *)mData, data, length))
        {
            mValid = true;
            delete[] data;
            return true;
        }

        std::memset(mData, 0, 64);
        ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to read public key");
        delete[] data;
        return false;
    }

    void PublicKey::getHash(Hash &pHash) const
    {
        // Calculate hash
        ArcMist::Digest digest(ArcMist::Digest::SHA256_RIPEMD160);
        write(&digest, true, false); // Compressed
        digest.getResult(&pHash);
    }

    void Signature::write(ArcMist::OutputStream *pStream, bool pScriptFormat) const
    {
        size_t length = 73;
        uint8_t output[length];
        if(!secp256k1_ecdsa_signature_serialize_der(mContext, output, &length, (secp256k1_ecdsa_signature*)mData))
            ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to write signature");
        if(pScriptFormat)
            ScriptInterpreter::writePushDataSize(length + 1, pStream);
        pStream->write(output, length);
        pStream->writeByte(mHashType);
    }

    bool Signature::read(ArcMist::InputStream *pStream, unsigned int pLength, bool pStrictECDSA_DER_Sigs)
    {
        uint8_t input[pLength + 2];
        unsigned int totalLength = pLength - 1;

        pStream->read(input, totalLength);
        mHashType = static_cast<Signature::HashType>(pStream->readByte());

#ifdef PROFILER_ON
        ArcMist::Profiler profiler("Signature Read");
#endif

        if(!pStrictECDSA_DER_Sigs)
        {
            // Hack badly formatted DER signatures
            uint8_t offset = 0;
            uint8_t subLength;
            if(input[offset++] != 0x30) // Compound header byte
            {
                ArcMist::String hex;
                hex.writeHex(input, totalLength);
                ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
                  "Invalid compound header byte in signature (%d bytes) : %s", totalLength, hex.text());
                return false;
            }

            // Full length
            unsigned int fullLengthOffset = offset;
            if(input[offset] != totalLength - 2)
            {
                if(input[offset] < totalLength - 2)
                {
                    // ArcMist::String hex;
                    // hex.writeHex(input, totalLength);
                    // ArcMist::Log::addFormatted(ArcMist::Log::DEBUG, BITCOIN_KEY_LOG_NAME,
                      // "Adjusting parse length %d to match total length in signature %d + 2 (header byte and length byte) : %s",
                      // totalLength, input[offset], hex.text());
                    totalLength = input[offset] + 2;
                }
                else
                {
                    ArcMist::String hex;
                    hex.writeHex(input, totalLength);
                    ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
                      "Invalid total length byte in signature (%d bytes) : %s", totalLength, hex.text());
                    return false;
                }
            }

            ++offset;

            // Integer header byte
            if(input[offset++] != 0x02)
            {
                ArcMist::String hex;
                hex.writeHex(input, totalLength);
                ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
                  "Invalid R integer header byte in signature (%d bytes) : %s", totalLength, hex.text());
                return false;
            }

            // R length
            subLength = input[offset++];
            if(subLength + offset > totalLength)
            {
                ArcMist::String hex;
                hex.writeHex(input, totalLength);
                ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
                  "R integer length byte too high in signature (%d bytes) : %s", totalLength, hex.text());
                return false;
            }

            while(input[offset] == 0x00 && !(input[offset+1] & 0x80))
            {
                // ArcMist::String hex;
                // hex.writeHex(input, totalLength);
                // ArcMist::Log::addFormatted(ArcMist::Log::DEBUG, BITCOIN_KEY_LOG_NAME,
                  // "Removing extra leading zero byte in R value from signature (%d bytes) : %s", totalLength, hex.text());

                // Adjust lengths
                input[offset-1]--;
                input[fullLengthOffset]--;

                // Extra padding. Remove this
                std::memmove(input + offset, input + offset + 1, totalLength - offset - 1);

                --totalLength;
                --subLength;
            }

            if(input[offset] & 0x80)
            {
                // ArcMist::String hex;
                // hex.writeHex(input, totalLength);
                // ArcMist::Log::addFormatted(ArcMist::Log::DEBUG, BITCOIN_KEY_LOG_NAME,
                  // "Adding required leading zero byte in R value to signature (%d bytes) : %s", totalLength, hex.text());

                // Adjust lengths
                input[offset-1]++;
                input[fullLengthOffset]++;

                // Add a zero byte
                std::memmove(input + offset + 1, input + offset, totalLength - offset);
                input[offset] = 0x00;

                ++totalLength;
                ++subLength;
            }

            offset += subLength;

            // Integer header byte
            if(input[offset++] != 0x02)
            {
                ArcMist::String hex;
                hex.writeHex(input, totalLength);
                ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
                  "Invalid S integer header byte in signature (%d bytes) : %s", totalLength, hex.text());
                return false;
            }

            // S length
            subLength = input[offset++];
            if(subLength + offset > totalLength)
            {
                ArcMist::String hex;
                hex.writeHex(input, totalLength);
                ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
                  "S integer length byte too high in signature (%d bytes) : %s", totalLength, hex.text());
                return false;
            }

            while(input[offset] == 0x00 && !(input[offset+1] & 0x80))
            {
                // ArcMist::String hex;
                // hex.writeHex(input, totalLength);
                // ArcMist::Log::addFormatted(ArcMist::Log::DEBUG, BITCOIN_KEY_LOG_NAME,
                  // "Removing extra leading zero byte in S value to signature (%d bytes) : %s", totalLength, hex.text());

                // Adjust lengths
                input[offset-1]--;
                input[fullLengthOffset]--;

                // Extra padding. Remove this
                std::memmove(input + offset, input + offset + 1, totalLength - offset - 1);

                --totalLength;
                --subLength;
            }

            if(input[offset] & 0x80)
            {
                // ArcMist::String hex;
                // hex.writeHex(input, totalLength);
                // ArcMist::Log::addFormatted(ArcMist::Log::DEBUG, BITCOIN_KEY_LOG_NAME,
                  // "Adding required leading zero byte in S value from signature (%d bytes) : %s", totalLength, hex.text());

                // Adjust lengths
                input[offset-1]++;
                input[fullLengthOffset]++;

                // Add a zero byte
                std::memmove(input + offset + 1, input + offset, totalLength - offset);
                input[offset] = 0x00;

                ++totalLength;
                ++subLength;
            }

            offset += subLength;
        }

        if(secp256k1_ecdsa_signature_parse_der(mContext, (secp256k1_ecdsa_signature*)mData, input, totalLength))
            return true;

        if(totalLength == 64 && !pStrictECDSA_DER_Sigs)
        {
            if(secp256k1_ecdsa_signature_parse_compact(mContext, (secp256k1_ecdsa_signature*)mData, input))
                return true;
            else
            {
                ArcMist::Log::add(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME, "Failed to parse compact signature (64 bytes)");
                return false;
            }
        }

        ArcMist::String hex;
        hex.writeHex(input, totalLength);
        ArcMist::Log::addFormatted(ArcMist::Log::VERBOSE, BITCOIN_KEY_LOG_NAME,
          "Failed to parse signature (%d bytes) : %s", totalLength, hex.text());
        return false;
    }

    bool Key::test()
    {
        ArcMist::Log::add(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "------------- Starting Key Tests -------------");

        bool success = true;
        PrivateKey privateKey;
        PublicKey publicKey;

        /***********************************************************************************************
         * Private Key Generate
         ***********************************************************************************************/
        if(privateKey.generate())
            ArcMist::Log::addFormatted(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Private Key Generate : %s", privateKey.hex().text());
        else
        {
            success = false;
            ArcMist::Log::addFormatted(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Private Key Generate : %s", privateKey.hex().text());
        }

        /***********************************************************************************************
         * Public Key Generate
         ***********************************************************************************************/
        if(privateKey.generatePublicKey(publicKey))
            ArcMist::Log::addFormatted(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Public Key Generate : %s", publicKey.hex().text());
        else
        {
            success = false;
            ArcMist::Log::addFormatted(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Public Key Generate : %s", publicKey.hex().text());
        }

        /***********************************************************************************************
         * Read Public Key
         ***********************************************************************************************/
        ArcMist::Buffer buffer;
        PublicKey readPublicKey;
        publicKey.write(&buffer, true, false);

        if(readPublicKey.read(&buffer))
            ArcMist::Log::addFormatted(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Read Public Key : %s", readPublicKey.hex().text());
        else
        {
            success = false;
            ArcMist::Log::addFormatted(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Read Public Key : %s", readPublicKey.hex().text());
        }

        /***********************************************************************************************
         * Read Public Key Compare
         ***********************************************************************************************/
        if(readPublicKey == publicKey)
            ArcMist::Log::addFormatted(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Read Public Key Compare : %s", readPublicKey.hex().text());
        else
        {
            success = false;
            ArcMist::Log::addFormatted(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Read Public Key Compare : %s", readPublicKey.hex().text());
        }

        /***********************************************************************************************
         * Sign Hash
         ***********************************************************************************************/
        Hash hash(32);
        Signature signature;
        hash.randomize(); // Generate random hash

        if(privateKey.sign(hash, signature))
            ArcMist::Log::addFormatted(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Sign Hash : %s", signature.hex().text());
        else
        {
            success = false;
            ArcMist::Log::addFormatted(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Sign Hash : %s", signature.hex().text());
        }

        /***********************************************************************************************
         * Verify signature
         ***********************************************************************************************/
        if(signature.verify(publicKey, hash))
            ArcMist::Log::add(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Verify Signature");
        else
        {
            success = false;
            ArcMist::Log::add(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Verify Signature");
        }

        /***********************************************************************************************
         * Verify Signature Incorrect
         ***********************************************************************************************/
        hash.zeroize();
        if(!signature.verify(publicKey, hash))
            ArcMist::Log::add(ArcMist::Log::INFO, BITCOIN_KEY_LOG_NAME, "Passed Verify Sign Incorrect");
        else
        {
            success = false;
            ArcMist::Log::add(ArcMist::Log::ERROR, BITCOIN_KEY_LOG_NAME, "Failed Verify Sign Incorrect");
        }

        return success;
    }
}
