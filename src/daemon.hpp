/**************************************************************************
 * Copyright 2017 ArcMist, LLC                                            *
 * Contributors :                                                         *
 *   Curtis Ellis <curtis@arcmist.com>                                    *
 * Distributed under the MIT software license, see the accompanying       *
 * file license.txt or http://www.opensource.org/licenses/mit-license.php *
 **************************************************************************/
#ifndef BITCOIN_DAEMON_HPP
#define BITCOIN_DAEMON_HPP

#include "arcmist/io/stream.hpp"
#include "arcmist/base/thread.hpp"
#include "arcmist/base/mutex.hpp"
#include "base.hpp"
#include "info.hpp"
#include "node.hpp"

#include <cstdint>
#include <vector>


namespace BitCoin
{
    class IPAddress;

    class Daemon
    {
    public:

        static Daemon &instance();
        static void destroy();

        // Threads
        static void handleConnections();
        static void manage();
        static void process();

        void run(ArcMist::String &pSeed, bool pInDaemonMode = true);

        bool start(bool pInDaemonMode);
        bool isRunning() { return mRunning; }
        bool stopping() { return mStopping; }

        void requestStop() { mStopRequested = true; mChain.requestStop(); }

        // Signals
        static void handleSigTermChild(int pValue);
        static void handleSigTerm(int pValue);
        static void handleSigInt(int pValue);
        static void handleSigPipe(int pValue);

    protected:

        static const unsigned int MAX_BLOCK_REQUEST = 16;

        Daemon();
        ~Daemon();

        Chain mChain;
        Info &mInfo;

        void stop();
        bool mRunning, mStopping, mStopRequested, mLoaded;

        // Threads
        ArcMist::Thread *mConnectionThread;
        ArcMist::Thread *mManagerThread;
        ArcMist::Thread *mProcessThread;

        // Timers
        uint32_t mLastHeaderRequestTime;

        // Signals
        void (*previousSigTermChildHandler)(int);
        void (*previousSigTermHandler)(int);
        void (*previousSigIntHandler)(int);
        void (*previousSigPipeHandler)(int);


        ArcMist::String mSeed;
        // Query peers from a seed
        // Returns number of peers actually connected
        unsigned int querySeed(const char *pName);

        // Nodes
        ArcMist::ReadersLock mNodeLock;
        std::vector<Node *> mNodes;
        unsigned int mNodeCount, mIncomingNodes, mOutgoingNodes;

        bool addNode(ArcMist::Network::Connection *pConnection, bool pIncoming, bool pIsSeed = false);
        unsigned int pickNodes(unsigned int pCount);
        void cleanNodes();

        Node *nodeWithInventory();
        Node *nodeWithBlock(const Hash &pHash);
        void sendRequests();
        void sendPeerRequest();
        unsigned int mLastPeerCount;
        void improvePing(int pDropFactor);

        Statistics mStatistics;
        void collectStatistics();
        void saveStatistics();
        void printStatistics();

        static Daemon *sInstance;
    };
}

#endif
