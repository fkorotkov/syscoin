// Copyright (c) 2019-2020 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSCOIN_LLMQ_QUORUMS_CHAINLOCKS_H
#define SYSCOIN_LLMQ_QUORUMS_CHAINLOCKS_H

#include <llmq/quorums.h>
#include <llmq/quorums_signing.h>

#include <chainparams.h>

#include <atomic>
#include <unordered_set>

#include <boost/thread.hpp>

class CBlockIndex;
class CScheduler;
class CConnman;
class PeerManager;
namespace llmq
{

class CChainLockSig
{
public:
    int32_t nHeight{-1};
    uint256 blockHash;
    CBLSSignature sig;

public:
    SERIALIZE_METHODS(CChainLockSig, obj) {
        READWRITE(obj.nHeight, obj.blockHash, obj.sig);
    }

    bool IsNull() const;
    std::string ToString() const;
};

class CChainLocksHandler : public CRecoveredSigsListener
{
    static const int64_t CLEANUP_INTERVAL = 1000 * 30;
    static const int64_t CLEANUP_SEEN_TIMEOUT = 24 * 60 * 60 * 1000;


private:
    CScheduler* scheduler;
    boost::thread* scheduler_thread;
    mutable RecursiveMutex cs;
    bool tryLockChainTipScheduled{false};
    bool isSporkActive{false};
    bool isEnforced{false};

    uint256 bestChainLockHash;
    CChainLockSig bestChainLock;

    CChainLockSig bestChainLockWithKnownBlock;
    const CBlockIndex* bestChainLockBlockIndex{nullptr};

    int32_t lastSignedHeight{-1};
    uint256 lastSignedRequestId;
    uint256 lastSignedMsgHash;

    std::map<uint256, int64_t> seenChainLocks;

    int64_t lastCleanupTime{0};

public:
    CConnman& connman;
    PeerManager& peerman;
    explicit CChainLocksHandler(CConnman &connman, PeerManager& peerman);
    ~CChainLocksHandler();

    void Start();
    void Stop();

    bool AlreadyHave(const uint256& hash);
    bool GetChainLockByHash(const uint256& hash, CChainLockSig& ret);
    CChainLockSig GetBestChainLock();

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    void ProcessNewChainLock(NodeId from, const CChainLockSig& clsig, const uint256& hash);
    void AcceptedBlockHeader(const CBlockIndex* pindexNew);
    void UpdatedBlockTip(const CBlockIndex* pindexNew);
    void CheckActiveState();
    void TrySignChainTip();
    void EnforceBestChainLock();
    virtual void HandleNewRecoveredSig(const CRecoveredSig& recoveredSig);

    bool HasChainLock(int nHeight, const uint256& blockHash);
    bool HasConflictingChainLock(int nHeight, const uint256& blockHash);


private:
    // these require locks to be held already
    bool InternalHasChainLock(int nHeight, const uint256& blockHash) EXCLUSIVE_LOCKS_REQUIRED(cs);
    bool InternalHasConflictingChainLock(int nHeight, const uint256& blockHash) EXCLUSIVE_LOCKS_REQUIRED(cs);

    static void DoInvalidateBlock(const CBlockIndex* pindex) LOCKS_EXCLUDED(::cs_main);

    void Cleanup();
};

extern CChainLocksHandler* chainLocksHandler;


} // namespace llmq

#endif // SYSCOIN_LLMQ_QUORUMS_CHAINLOCKS_H
