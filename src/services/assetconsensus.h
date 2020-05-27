﻿// Copyright (c) 2017-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSCOIN_SERVICES_ASSETCONSENSUS_H
#define SYSCOIN_SERVICES_ASSETCONSENSUS_H
#include <primitives/transaction.h>
#include <dbwrapper.h>
class TxValidationState;
class CCoinsViewCache;
class EthereumTxRoot {
    public:
    std::vector<unsigned char> vchBlockHash;
    std::vector<unsigned char> vchPrevHash;
    std::vector<unsigned char> vchTxRoot;
    std::vector<unsigned char> vchReceiptRoot;
    int64_t nTimestamp;
    
    SERIALIZE_METHODS(EthereumTxRoot, obj)
    {
        READWRITE(obj.vchBlockHash, obj.vchPrevHash, obj.vchTxRoot, obj.vchReceiptRoot, obj.nTimestamp);
    }
};
typedef std::unordered_map<uint32_t, EthereumTxRoot> EthereumTxRootMap;
class CEthereumTxRootsDB : public CDBWrapper {
public:
    CEthereumTxRootsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "ethereumtxroots", nCacheSize, fMemory, fWipe) {
       Init();
    } 
    bool ReadTxRoots(const uint32_t& nHeight, EthereumTxRoot& txRoot) {
        return Read(nHeight, txRoot);
    } 
    void AuditTxRootDB(std::vector<std::pair<uint32_t, uint32_t> > &vecMissingBlockRanges);
    bool Init();
    bool Clear();
    bool PruneTxRoots(const uint32_t &fNewGethSyncHeight);
    bool FlushErase(const std::vector<uint32_t> &vecHeightKeys);
    bool FlushWrite(const EthereumTxRootMap &mapTxRoots);
};

class CEthereumMintedTxDB : public CDBWrapper {
public:
    CEthereumMintedTxDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "ethereumminttx", nCacheSize, fMemory, fWipe) {
    } 
    bool FlushErase(const EthereumMintTxMap &mapMintKeys);
    bool FlushWrite(const EthereumMintTxMap &mapMintKeys);
};

class CAssetDB : public CDBWrapper {
public:
    CAssetDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assets", nCacheSize, fMemory, fWipe) {}
    bool EraseAsset(const uint32_t& nAsset) {
        return Erase(nAsset);
    }   
    bool ReadAsset(const uint32_t& nAsset, CAsset& asset) {
        return Read(nAsset, asset);
    }  
    bool Flush(const AssetMap &mapAssets);
};
extern std::unique_ptr<CAssetDB> passetdb;
extern std::unique_ptr<CEthereumTxRootsDB> pethereumtxrootsdb;
extern std::unique_ptr<CEthereumMintedTxDB> pethereumtxmintdb;
bool DisconnectAssetActivate(const CTransaction &tx, const uint256& txHash, AssetMap &mapAssets);
bool DisconnectAssetSend(const CTransaction &tx, const uint256& txHash, AssetMap &mapAssets);
bool DisconnectAssetUpdate(const CTransaction &tx, const uint256& txHash, AssetMap &mapAssets);
bool DisconnectMintAsset(const CTransaction &tx, const uint256& txHash, EthereumMintTxMap &mapMintKeys);
bool DisconnectSyscoinTransaction(const CTransaction& tx, const uint256& txHash, CCoinsViewCache& view, AssetMap &mapAssets, EthereumMintTxMap &mapMintKeys);
bool CheckSyscoinMint(const bool &ibd, const CTransaction& tx, const uint256& txHash, TxValidationState &tstate, const bool &fJustCheck, const bool& bSanityCheck, const int& nHeight, const int64_t& nTime, const uint256& blockhash, EthereumMintTxMap &mapMintKeys);
bool CheckAssetInputs(const CTransaction &tx, const uint256& txHash, TxValidationState &tstate, const bool &fJustCheck, const int &nHeight, const uint256& blockhash, AssetMap &mapAssets, const bool &bSanityCheck=false);
bool CheckSyscoinInputs(const CTransaction& tx, const uint256& txHash, TxValidationState &tstate, const int &nHeight, const int64_t& nTime, EthereumMintTxMap &mapMintKeys);
bool CheckSyscoinInputs(const bool &ibd, const CTransaction& tx,  const uint256& txHash, TxValidationState &tstate, const bool &fJustCheck, const int &nHeight, const int64_t& nTime, const uint256 & blockHash, const bool &bSanityCheck, AssetMap &mapAssets, EthereumMintTxMap &mapMintKeys);
bool CheckAssetAllocationInputs(const CTransaction &tx, const uint256& txHash, TxValidationState &tstate, const bool &fJustCheck, const int &nHeight, const uint256& blockhash, const bool &bSanityCheck = false);
bool FormatSyscoinErrorMessage(TxValidationState &state, const std::string errorMessage, bool bErrorNotInvalid = true, bool bConsensus = true);
bool FlushSyscoinDBs();
#endif // SYSCOIN_SERVICES_ASSETCONSENSUS_H
