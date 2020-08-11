// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSCOIN_MASTERNODE_MASTERNODE_PAYMENTS_H
#define SYSCOIN_MASTERNODE_MASTERNODE_PAYMENTS_H

#include <util/system.h>
#include <core_io.h>
#include <key.h>
#include <net_processing.h>
#include <util/strencodings.h>

#include <evo/deterministicmns.h>

class CMasternodePayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, const CAmount &blockReward, std::string& strErrorRet);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, const CAmount &blockReward, const CAmount &fees, CAmount& nMNSeniorityRet);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, const CAmount &blockReward, const CAmount &fees, std::vector<CTxOut>& voutMasternodePaymentsRet, std::vector<CTxOut>& voutSuperblockPaymentsRet);
std::map<int, std::string> GetRequiredPaymentsStrings(int nStartHeight, int nEndHeight);

extern CMasternodePayments mnpayments;

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
public:
    static bool GetBlockTxOuts(int nBlockHeight, const CAmount &blockReward, std::vector<CTxOut>& voutMasternodePaymentsRet, const CAmount &nHalfFee, CAmount& nMNSeniorityRet, int& nCollateralHeight);
    static bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight, const CAmount &blockReward, const CAmount& nHalfFee, CAmount& nMNSeniorityRet);

    static bool GetMasternodeTxOuts(int nBlockHeight, const CAmount &blockReward, std::vector<CTxOut>& voutMasternodePaymentsRet, const CAmount &nHalfFee);
};

#endif
