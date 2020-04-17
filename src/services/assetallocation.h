﻿// Copyright (c) 2017-2018 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSCOIN_SERVICES_ASSETALLOCATION_H
#define SYSCOIN_SERVICES_ASSETALLOCATION_H

#include <dbwrapper.h>
#include <primitives/transaction.h>
#include <compressor.h>
#include <amount.h>
class CTransaction;
class CAsset;
class CMintSyscoin;
bool AssetMintTxToJson(const CTransaction& tx, const uint256& txHash, UniValue &entry);
std::string assetAllocationFromTx(const int &nVersion);
static const int ONE_YEAR_IN_BLOCKS = 525600;
static const int ONE_HOUR_IN_BLOCKS = 60;
static const int ONE_MONTH_IN_BLOCKS = 43800;
enum {
	ZDAG_NOT_FOUND = -1,
	ZDAG_STATUS_OK = 0,
	ZDAG_WARNING_RBF,
    ZDAG_WARNING_NOT_ZDAG_TX,
    ZDAG_WARNING_SIZE_OVER_POLICY,
	ZDAG_MAJOR_CONFLICT
};
class CAssetOut {
public:
    uint32_t n;
    CAmount nValue;

    SERIALIZE_METHODS(CAssetOut, obj)
    {
        READWRITE(VARINT(obj.n));
        READWRITE(Using<AmountCompression>(obj.nValue));
    }
    CAssetOut(const uint32_t &nIn, const CAmount& nAmountIn): n(nIn), nValue(nAmountIn) {}
	CAssetOut() {
		nValue = 0;
        n = 0;
	}
    inline friend bool operator==(const CAssetOut &a, const CAssetOut &b) {
		return (a.n == b.n && a.nValue == b.nValue);
	}
    inline friend bool operator!=(const CAssetOut &a, const CAssetOut &b) {
		return !(a == b);
	}
};
typedef std::map<int32_t, std::vector<CAssetOut> > assetOutputType;
class CAssetAllocation {
public:
    assetOutputType voutAssets;
    SERIALIZE_METHODS(CAssetAllocation, obj)
    {
        READWRITE(obj.voutAssets);
    }

	CAssetAllocation() {
		SetNull();
	}
	explicit CAssetAllocation(const CTransaction &tx) {
		SetNull();
		UnserializeFromTx(tx);
	}
	
	inline friend bool operator==(const CAssetAllocation &a, const CAssetAllocation &b) {
		return (a.voutAssets == b.voutAssets
			);
	}
    CAssetAllocation(const CAssetAllocation&) = delete;
    CAssetAllocation(CAssetAllocation && other) = default;
    CAssetAllocation& operator=( CAssetAllocation& a ) = delete;
	CAssetAllocation& operator=( CAssetAllocation&& a ) = default;
 
	inline friend bool operator!=(const CAssetAllocation &a, const CAssetAllocation &b) {
		return !(a == b);
	}
	inline void SetNull() { voutAssets.clear();}
    inline bool IsNull() const { return voutAssets.empty();}
	bool UnserializeFromTx(const CTransaction &tx);
	bool UnserializeFromData(const std::vector<unsigned char> &vchData);
	void Serialize(std::vector<unsigned char>& vchData);
};
class CMintSyscoin {
public:
    CAssetAllocation assetAllocation;
    std::vector<unsigned char> vchTxValue;
    std::vector<unsigned char> vchTxParentNodes;
    std::vector<unsigned char> vchTxRoot;
    std::vector<unsigned char> vchTxPath;
    std::vector<unsigned char> vchReceiptValue;
    std::vector<unsigned char> vchReceiptParentNodes;
    std::vector<unsigned char> vchReceiptRoot;
    std::vector<unsigned char> vchReceiptPath;   
    uint32_t nBlockNumber;
    uint32_t nBridgeTransferID;

    CMintSyscoin() {
        SetNull();
    }
    explicit CMintSyscoin(const CTransaction &tx) {
        SetNull();
        UnserializeFromTx(tx);
    }
    SERIALIZE_METHODS(CMintSyscoin, obj)
    {
        READWRITE(obj.assetAllocation);
        READWRITE(obj.nBridgeTransferID); 
        READWRITE(obj.vchTxValue);
        READWRITE(obj.vchTxParentNodes);
        READWRITE(obj.vchTxRoot);
        READWRITE(obj.vchTxPath);   
        READWRITE(obj.vchReceiptValue);
        READWRITE(obj.vchReceiptParentNodes);
        READWRITE(obj.vchReceiptRoot);
        READWRITE(obj.vchReceiptPath);
        READWRITE(obj.nBlockNumber); 
    }
    inline void SetNull() { assetAllocation.SetNull(); vchTxRoot.clear(); vchTxValue.clear(); vchTxParentNodes.clear(); vchTxPath.clear(); vchReceiptRoot.clear(); vchReceiptValue.clear(); vchReceiptParentNodes.clear(); vchReceiptPath.clear(); nBridgeTransferID = 0; nBlockNumber = 0;  }
    inline bool IsNull() const { return (vchTxValue.empty() && vchReceiptValue.empty()); }
    bool UnserializeFromData(const std::vector<unsigned char> &vchData);
    bool UnserializeFromTx(const CTransaction &tx);
    void Serialize(std::vector<unsigned char>& vchData);
};
class CBurnSyscoin {
public:
    CAssetAllocation assetAllocation;
    std::vector<unsigned char> vchEthAddress;
    CBurnSyscoin() {
        SetNull();
    }
    explicit CBurnSyscoin(const CTransaction &tx) {
        SetNull();
        UnserializeFromTx(tx);
    }
    SERIALIZE_METHODS(CBurnSyscoin, obj) {
        READWRITE(obj.assetAllocation);
        READWRITE(obj.vchEthAddress);     
    }

    inline void SetNull() { assetAllocation.SetNull(); vchEthAddress.clear();  }
    inline bool IsNull() const { return (vchEthAddress.empty() && assetAllocation.IsNull()); }
    bool UnserializeFromData(const std::vector<unsigned char> &vchData);
    bool UnserializeFromTx(const CTransaction &tx);
    void Serialize(std::vector<unsigned char>& vchData);
};
bool AssetAllocationTxToJSON(const CTransaction &tx, UniValue &entry);
#endif // SYSCOIN_SERVICES_ASSETALLOCATION_H
