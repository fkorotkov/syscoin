// Copyright (c) 2017-2019 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DASH_CBTX_H
#define DASH_CBTX_H

#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "univalue.h"

class CBlock;
class CBlockIndex;

// coinbase transaction
class CCbTx
{
public:
    static const uint16_t CURRENT_VERSION = 2;

public:
    uint16_t nVersion{CURRENT_VERSION};
    uint256 merkleRootMNList;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(merkleRootMNList);
    }

    std::string ToString() const;

    void ToJson(UniValue& obj) const
    {
        obj.clear();
        obj.setObject();
        obj.push_back(Pair("version", (int)nVersion));
        obj.push_back(Pair("merkleRootMNList", merkleRootMNList.ToString()));
    }
};

bool CheckCbTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

bool CheckCbTxMerkleRoots(const CBlock& block, const CBlockIndex* pindex, CValidationState& state);
bool CalcCbTxMerkleRootMNList(const CBlock& block, const CBlockIndex* pindexPrev, uint256& merkleRootRet, CValidationState& state);

#endif //DASH_CBTX_H
