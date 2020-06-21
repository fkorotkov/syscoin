// Copyright (c) 2018-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_init.h>

#include <llmq/quorums.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_debug.h>
#include <llmq/quorums_dkgsessionmgr.h>
#include <llmq/quorums_signing.h>
#include <llmq/quorums_signing_shares.h>

#include <dbwrapper.h>
#include <net.h>
namespace llmq
{

CBLSWorker* blsWorker;

CDBWrapper* llmqDb;

void InitLLMQSystem(CEvoDB& evoDb, bool unitTests, bool fWipe, CConnman& connman)
{
    llmqDb = new CDBWrapper(unitTests ? "" : (GetDataDir() / "llmq"), 1 << 20, unitTests, fWipe);
    blsWorker = new CBLSWorker();

    quorumDKGDebugManager = new CDKGDebugManager();
    quorumBlockProcessor = new CQuorumBlockProcessor(evoDb, connman);
    quorumDKGSessionManager = new CDKGSessionManager(*llmqDb, *blsWorker, connman);
    quorumManager = new CQuorumManager(evoDb, *blsWorker, *quorumDKGSessionManager, connman);
    quorumSigSharesManager = new CSigSharesManager(connman);
    quorumSigningManager = new CSigningManager(*llmqDb, unitTests, connman);
}

void DestroyLLMQSystem()
{
    delete quorumSigningManager;
    quorumSigningManager = nullptr;
    delete quorumSigSharesManager;
    quorumSigSharesManager = nullptr;
    delete quorumManager;
    quorumManager = nullptr;
    delete quorumDKGSessionManager;
    quorumDKGSessionManager = nullptr;
    delete quorumBlockProcessor;
    quorumBlockProcessor = nullptr;
    delete quorumDKGDebugManager;
    quorumDKGDebugManager = nullptr;
    delete blsWorker;
    blsWorker = nullptr;
    delete llmqDb;
    llmqDb = nullptr;
}

void StartLLMQSystem(CConnman &connman)
{
    if (blsWorker) {
        blsWorker->Start();
    }
    if (quorumDKGSessionManager) {
        quorumDKGSessionManager->StartMessageHandlerPool(connman);
    }
    if (quorumSigSharesManager) {
        quorumSigSharesManager->RegisterAsRecoveredSigsListener();
        quorumSigSharesManager->StartWorkerThread();
    }
}

void StopLLMQSystem()
{
    if (quorumSigSharesManager) {
        quorumSigSharesManager->StopWorkerThread();
        quorumSigSharesManager->UnregisterAsRecoveredSigsListener();
    }
    if (quorumDKGSessionManager) {
        quorumDKGSessionManager->StopMessageHandlerPool();
    }
    if (blsWorker) {
        blsWorker->Stop();
    }
}

void InterruptLLMQSystem()
{
    if (quorumSigSharesManager) {
        quorumSigSharesManager->InterruptWorkerThread();
    }
}

} // namespace llmq
