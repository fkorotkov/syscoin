// Copyright (c) 2018-2019 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <clientversion.h>
#include <consensus/validation.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <validation.h>

#include <evo/cbtx.h>
#include <evo/deterministicmns.h>
#include <evo/specialtx.h>

#include <llmq/quorums_commitment.h>
#include <llmq/quorums_blockprocessor.h>

bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state, bool fJustCheck)
{

    try {
        switch (tx.nVersion) {
        case SYSCOIN_TX_VERSION_MN_PROVIDER_REGISTER:
            return CheckProRegTx(tx, pindexPrev, state, fJustCheck);
        case SYSCOIN_TX_VERSION_MN_UPDATE_SERVICE:
            return CheckProUpServTx(tx, pindexPrev, state, fJustCheck);
        case SYSCOIN_TX_VERSION_MN_UPDATE_REGISTRAR:
            return CheckProUpRegTx(tx, pindexPrev, state, fJustCheck);
        case SYSCOIN_TX_VERSION_MN_UPDATE_REVOKE:
            return CheckProUpRevTx(tx, pindexPrev, state, fJustCheck);
        case SYSCOIN_TX_VERSION_MN_COINBASE:
            return CheckCbTx(tx, pindexPrev, state, fJustCheck);
        case SYSCOIN_TX_VERSION_MN_QUORUM_COMMITMENT:
            return llmq::CheckLLMQCommitment(tx, pindexPrev, state, fJustCheck);
        default:
            return true;
        }
    } catch (const std::exception& e) {
        LogPrintf("%s -- failed: %s\n", __func__, e.what());
        return FormatSyscoinErrorMessage(state, "failed-check-special-tx", fJustCheck);
    }

    return FormatSyscoinErrorMessage(state, "bad-tx-type-check", fJustCheck);
}

bool IsSpecialTx(const CTransaction& tx)
{
    switch (tx.nVersion) {
    case SYSCOIN_TX_VERSION_MN_PROVIDER_REGISTER:
    case SYSCOIN_TX_VERSION_MN_UPDATE_SERVICE:
    case SYSCOIN_TX_VERSION_MN_UPDATE_REGISTRAR:
    case SYSCOIN_TX_VERSION_MN_UPDATE_REVOKE:
    case SYSCOIN_TX_VERSION_MN_COINBASE:
    case SYSCOIN_TX_VERSION_MN_QUORUM_COMMITMENT:
        return true;
    default:
        return false;
    }

    return false;
}

bool ProcessSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state, bool fJustCheck, bool fCheckCbTxMerleRoots)
{
    static int64_t nTimeLoop = 0;
    static int64_t nTimeQuorum = 0;
    static int64_t nTimeDMN = 0;
    static int64_t nTimeMerkle = 0;

    try {
        int64_t nTime1 = GetTimeMicros();

        for (size_t i = 0; i < block.vtx.size(); i++) {
            const CTransaction& tx = *block.vtx[i];
            if (!CheckSpecialTx(tx, pindex->pprev, state, false)) {
                // pass the state returned by the function above
                return false;
            }
        }

        int64_t nTime2 = GetTimeMicros(); nTimeLoop += nTime2 - nTime1;
        LogPrint(BCLog::BENCH, "        - Loop: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeLoop * 0.000001);

        if (!llmq::quorumBlockProcessor->ProcessBlock(block, pindex, state)) {
            // pass the state returned by the function above
            return false;
        }

        int64_t nTime3 = GetTimeMicros(); nTimeQuorum += nTime3 - nTime2;
        LogPrint(BCLog::BENCH, "        - quorumBlockProcessor: %.2fms [%.2fs]\n", 0.001 * (nTime3 - nTime2), nTimeQuorum * 0.000001);

        if (!deterministicMNManager->ProcessBlock(block, pindex, state)) {
            // pass the state returned by the function above
            return false;
        }

        int64_t nTime4 = GetTimeMicros(); nTimeDMN += nTime4 - nTime3;
        LogPrint(BCLog::BENCH, "        - deterministicMNManager: %.2fms [%.2fs]\n", 0.001 * (nTime4 - nTime3), nTimeDMN * 0.000001);

        if (fCheckCbTxMerleRoots && !CheckCbTxMerkleRoots(block, pindex, state)) {
            // pass the state returned by the function above
            return false;
        }

        int64_t nTime5 = GetTimeMicros(); nTimeMerkle += nTime5 - nTime4;
        LogPrint(BCLog::BENCH, "        - CheckCbTxMerkleRoots: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeMerkle * 0.000001);
    } catch (const std::exception& e) {
        LogPrintf(strprintf("%s -- failed: %s\n", __func__, e.what()).c_str());
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "failed-procspectxsinblock");
    }

    return true;
}

bool UndoSpecialTxsInBlock(const CBlock& block, const CBlockIndex* pindex)
{
    try {
        if (!deterministicMNManager->UndoBlock(block, pindex)) {
            return false;
        }

        if (!llmq::quorumBlockProcessor->UndoBlock(block, pindex)) {
            return false;
        }
    } catch (const std::exception& e) {
        return error(strprintf("%s -- failed: %s\n", __func__, e.what()).c_str());
    }

    return true;
}

uint256 CalcTxInputsHash(const CTransaction& tx)
{
    CHashWriter hw(CLIENT_VERSION, SER_GETHASH);
    for (const auto& in : tx.vin) {
        hw << in.prevout;
    }
    return hw.GetHash();
}
