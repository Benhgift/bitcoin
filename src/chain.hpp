/**************************************************************************
 * Copyright 2017 ArcMist, LLC                                            *
 * Contributors :                                                         *
 *   Curtis Ellis <curtis@arcmist.com>                                    *
 * Distributed under the MIT software license, see the accompanying       *
 * file license.txt or http://www.opensource.org/licenses/mit-license.php *
 **************************************************************************/
#ifndef BITCOIN_CHAIN_HPP
#define BITCOIN_CHAIN_HPP

#include "arcmist/base/string.hpp"
#include "arcmist/base/mutex.hpp"
#include "base.hpp"
#include "forks.hpp"
#include "block.hpp"
#include "outputs.hpp"
#include "mem_pool.hpp"

#include <list>
#include <vector>
#include <stdlib.h>


namespace BitCoin
{
    class BlockInfo
    {
    public:
        BlockInfo(Hash *pHash, unsigned int pFileID, unsigned int pHeight)
        {
            hash   = pHash;
            fileID = pFileID;
            height = pHeight;
        }

        Hash        *hash;
        unsigned int fileID;
        unsigned int height;

    private:
        BlockInfo(BlockInfo &pCopy);
        BlockInfo &operator = (BlockInfo &pRight);
    };

    class BlockSet : public std::list<BlockInfo *>, public ArcMist::Mutex
    {
    public:
        BlockSet() : Mutex("Block Set") {}
        ~BlockSet()
        {
            for(iterator info=begin();info!=end();++info)
                delete *info;
        }

        bool contains(const Hash &pHash) const
        {
            for(const_iterator info=begin();info!=end();++info)
                if(*(*info)->hash == pHash)
                    return true;
            return false;
        }

        void clear()
        {
            for(iterator info=begin();info!=end();++info)
                delete *info;
            std::list<BlockInfo *>::clear();
        }

        bool remove(const Hash &pHash)
        {
            for(iterator info=begin();info!=end();++info)
                if(*(*info)->hash == pHash)
                {
                    delete *info;
                    erase(info);
                    return true;
                }

            return false;
        }

    private:
        BlockSet(BlockSet &pCopy);
        BlockSet &operator = (BlockSet &pRight);
    };

    class PendingHeaderData
    {
    public:

        PendingHeaderData(const Hash &pHash, unsigned int pNodeID, uint32_t pTime)
        {
            hash = pHash;
            requestedTime = pTime;
            requestingNode = pNodeID;
        }

        Hash hash;
        uint32_t requestedTime;
        unsigned int requestingNode;

    private:
        PendingHeaderData(PendingHeaderData &pCopy);
        PendingHeaderData &operator = (PendingHeaderData &pRight);
    };

    class PendingBlockData
    {
    public:

        PendingBlockData(Block *pBlock)
        {
            block = pBlock;
            requestedTime = 0;
            updateTime = 0;
            requestingNode = 0;
        }
        ~PendingBlockData()
        {
            if(block != NULL)
                delete block;
        }

        void replace(Block *pBlock)
        {
            if(block != NULL)
                delete block;
            block = pBlock;
        }

        // Return true if this is a full block and not just a header
        bool isFull() { return block->transactionCount > 0; }

        Block *block;
        uint32_t requestedTime;
        uint32_t updateTime;
        unsigned int requestingNode;

    private:
        PendingBlockData(PendingBlockData &pCopy);
        PendingBlockData &operator = (PendingBlockData &pRight);
    };

    /* Branches
     * When a valid header is seen that doesn't link to the top of the current chain it is
     *   saved and built on.
     * If it builds to more proof of work than the current chain before it gets too old then
     *   revert the current chain to the height of the branch and apply the branch. Also, turn
     *   the previous chain before above the branch into a branch in case it flips back and
     *   forth.
     */
    class Branch
    {
    public:

        Branch(unsigned int pHeight, const Hash &pWork) : accumulatedWork(pWork) { height = pHeight + 1; }
        ~Branch();

        void addBlock(Block *pBlock)
        {
            pendingBlocks.push_back(new PendingBlockData(pBlock));
            Hash work(32);
            Hash target(32);
            target.setDifficulty(pBlock->targetBits);
            target.getWork(work);
            accumulatedWork += work;
        }

        unsigned int height; // The chain height of the first block in the branch
        std::list<PendingBlockData *> pendingBlocks;
        Hash accumulatedWork;
    };

    class Chain
    {
    public:

        Chain();
        ~Chain();

        int height() const { return mNextBlockHeight - 1; }
        const Hash &lastBlockHash() const { return mLastBlockHash; }
        unsigned int pendingChainHeight() const { return mNextBlockHeight - 1 + mPendingBlocks.size(); }
        const Hash &lastPendingBlockHash() const { if(!mLastPendingHash.isEmpty()) return mLastPendingHash; return mLastBlockHash; }
        unsigned int highestFullPendingHeight() const { return mLastFullPendingOffset + mNextBlockHeight - 1; }
        const Hash &accumulatedWork() { return mBlockStats.accumulatedWork(mBlockStats.height()); }
        const Hash &pendingAccumulatedWork() { return mPendingAccumulatedWork; }

        TransactionOutputPool &outputs() { return mOutputs; }
        const BlockStats &blockStats() const { return mBlockStats; }
        const Forks &forks() const { return mForks; }
        MemPool &memPool() { return mMemPool; }

        unsigned int branchCount() const { return mBranches.size(); }
        const Branch *branchAt(unsigned int pOffset) const
        {
            if(mBranches.size() <= pOffset)
                return NULL;
            else
                return mBranches[pOffset];
        }

        // Chain is up to date with most chains
        bool isInSync() { return mIsInSync; }
        Block *blockToAnnounce();

        // Check if a block is already in the chain
        bool blockInChain(const Hash &pHash) const { return mBlockLookup[pHash.lookup16()].contains(pHash); }
        // Check if a header has been downloaded
        bool headerAvailable(const Hash &pHash);

        // Branches
        bool headerInBranch(const Hash &pHash);

        // Return true if a header request at the top of the chain is needed
        bool headersNeeded();
        // Return true if a block request is needed
        bool blocksNeeded();

        // Number of pending headers/blocks
        unsigned int pendingCount();
        // Number of pending full blocks
        unsigned int pendingBlockCount();
        // Bytes used by pending blocks
        unsigned int pendingSize();

        enum HashStatus { ALREADY_HAVE, NEED_HEADER, NEED_BLOCK, BLACK_LISTED };

        // Return the status of the specified block hash
        HashStatus addPendingHash(const Hash &pHash, unsigned int pNodeID);

        // Builds a list of blocks that need to be requested and marks them as requested by the node specified
        bool getBlocksNeeded(HashList &pHashes, unsigned int pCount, bool pReduceOnly);
        // Mark that download progress has increased for this block
        void updateBlockProgress(const Hash &pHash, unsigned int pNodeID, uint32_t pTime);
        // Mark blocks as requested by the specified node
        void markBlocksForNode(HashList &pHashes, unsigned int pNodeID);
        // Release all blocks requested by a specified node so they will be requested again
        void releaseBlocksForNode(unsigned int pNodeID);

        // Add block to queue to be processed and added to top of chain
        bool addPendingBlock(Block *pBlock);

        // Retrieve block hashes starting at a specific hash. (empty starting hash for first block)
        bool getBlockHashes(HashList &pHashes, const Hash &pStartingHash, unsigned int pCount);
        // Retrieve list of block hashes starting at top, going down and skipping around 100 between each.
        bool getReverseBlockHashes(HashList &pHashes, unsigned int pCount);

        // Retrieve block headers starting at a specific hash. (empty starting hash for first block)
        bool getBlockHeaders(BlockList &pBlockHeaders, const Hash &pStartingHash, const Hash &pStoppingHash, unsigned int pCount);

        // Get block or hash at specific height
        bool getBlockHash(unsigned int pHeight, Hash &pHash);
        bool getBlock(unsigned int pHeight, Block &pBlock);
        bool getHeader(unsigned int pHeight, Block &pBlockHeader);

        // Get the block or height for a specific hash
        int blockHeight(const Hash &pHash); // Returns -1 when hash is not found
        bool getBlock(const Hash &pHash, Block &pBlock);
        bool getHeader(const Hash &pHash, Block &pBlockHeader);

        // Load block data from file system
        //   If pList is true then all the block hashes will be output
        bool load(bool pPreCacheOutputs = true);
        bool save();

        // Process pending headers and blocks
        void process();

        // Validate the local block chain. Print output to log
        //   If pRebuildUnspent then it rebuilds unspent transactions
        bool validate(bool pRebuild);

        std::vector<unsigned int> blackListedNodeIDs();

        // Set flag to stop processing
        void requestStop() { mStop = true; }

        // For testing only
        void setMaxTargetBits(uint32_t pMaxTargetBits) { mMaxTargetBits = pMaxTargetBits; }
        static bool test();
        static void tempTest();

    private:

        TransactionOutputPool mOutputs;
        HashList mBlockHashes;
        BlockSet mBlockLookup[0x10000];

        // Block headers for blocks not yet on chain
        ArcMist::ReadersLock mPendingLock;
        std::list<PendingBlockData *> mPendingBlocks;
        Hash mLastPendingHash, mPendingAccumulatedWork;
        unsigned int mPendingSize, mPendingBlockCount, mLastFullPendingOffset;
        uint32_t mBlockProcessStartTime;

        // Save pending data to the file system
        bool savePending();
        // Load pending data from the file system
        bool loadPending();

        // Update the unspent transaction pool for any blocks it is missing
        bool updateOutputs();

        // Verify and process block then add it to the chain
        ArcMist::Mutex mProcessMutex;
        bool mStop;
        bool mIsInSync;
        bool mAnnouncedAdded;

        bool processBlock(Block *pBlock);

        // Revert to a lower height
        bool revert(int pHeight);
        bool revertBlockFileHeight(int pHeight);

        static const unsigned int INVALID_FILE_ID = 0xffffffff;
        unsigned int blockFileID(const Hash &pHash);

        Hash mLastBlockHash; // Hash of last/top block on chain
        int32_t mNextBlockHeight; // Number of next block that will be added to the chain
        BlockFile *mLastBlockFile;
        unsigned int mLastFileID;

        // Target
        uint32_t mMaxTargetBits;
        uint32_t mTargetBits; // Current target bits

        // Update target bits based on new block
        bool updateTargetBits();

        // Last BLOCK_STATS_SIZE block's statistics
        Forks mForks;
        BlockStats mBlockStats;
        MemPool mMemPool;
        uint64_t mAccumulatedProofOfWork;

        std::list<PendingHeaderData *> mPendingHeaders;
        HashList mBlocksToAnnounce;
        Block *mAnnounceBlock;

        HashList mBlackListBlocks;
        std::vector<unsigned int> mBlackListedNodeIDs;

        void addBlackListedBlock(const Hash &pHash);

        std::vector<Branch *> mBranches;

        // Check if a branch has more accumulated proof of work than the main chain
        bool checkBranches();

        static Chain *sInstance;

    };
}

#endif
