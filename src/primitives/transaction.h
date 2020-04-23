// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SYSCOIN_PRIMITIVES_TRANSACTION_H
#define SYSCOIN_PRIMITIVES_TRANSACTION_H

#include <stdint.h>
#include <amount.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>
class TxValidationState;
class CAsset;
static const int SERIALIZE_TRANSACTION_NO_WITNESS = 0x40000000;
/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    static constexpr uint32_t NULL_INDEX = std::numeric_limits<uint32_t>::max();

    COutPoint(): n(NULL_INDEX) { }
    COutPoint(const uint256& hashIn, uint32_t nIn): hash(hashIn), n(nIn) { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull() { hash.SetNull(); n = NULL_INDEX; }
    bool IsNull() const { return (hash.IsNull() && n == NULL_INDEX); }

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        int cmp = a.hash.Compare(b.hash);
        return cmp < 0 || (cmp == 0 && a.n < b.n);
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
    // SYSCOIN
    std::string ToStringShort() const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;
    CScriptWitness scriptWitness; //!< Only serialized through CTransaction

    /* Setting nSequence to this value for every input in a transaction
     * disables nLockTime. */
    static const uint32_t SEQUENCE_FINAL = 0xffffffff;

    /* Below flags apply in the context of BIP 68*/
    /* If this flag set, CTxIn::nSequence is NOT interpreted as a
     * relative lock-time. */
    static const uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1U << 31);

    /* If CTxIn::nSequence encodes a relative lock-time and this flag
     * is set, the relative lock-time has units of 512 seconds,
     * otherwise it specifies blocks with a granularity of 1. */
    static const uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);

    /* If CTxIn::nSequence encodes a relative lock-time, this mask is
     * applied to extract that lock-time from the sequence field. */
    static const uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;

    /* In order to use the same number of bits to encode roughly the
     * same wall-clock duration, and because blocks are naturally
     * limited to occur every 600s on average, the minimum granularity
     * for time-based relative lock-time is fixed at 512 seconds.
     * Converting from CTxIn::nSequence to seconds is performed by
     * multiplying by 512 = 2^9, or equivalently shifting up by
     * 9 bits. */
    static const int SEQUENCE_LOCKTIME_GRANULARITY = 9;

    CTxIn()
    {
        nSequence = SEQUENCE_FINAL;
    }

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
};

class CAssetCoinInfo {
public:
	int32_t nAsset;
	CAmount nValue;
	CAssetCoinInfo() {
		SetNull();
        nValue = 0;
	}
    CAssetCoinInfo(const int32_t &nAssetIn, const CAmount& nValueIn): nAsset(nAssetIn), nValue(nValueIn) { }
 
    friend bool operator==(const CAssetCoinInfo& a, const CAssetCoinInfo& b)
    {
        return (a.nAsset   == b.nAsset &&
                a.nValue == b.nValue);
    }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nAsset);
        if(nAsset > 0)
            READWRITE(nValue);
    }
	inline void SetNull() { nAsset = 0; }
    inline bool IsNull() const { return nAsset == 0;}
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;
    // SYSCOIN
    CAssetCoinInfo assetInfo;
    CTxOut()
    {
        SetNull();
    }

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    }

    void SetNull()
    {
        assetInfo.SetNull();
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }
    std::string ToString() const;
};
// SYSCOIN
class CTxOutCoin: public CTxOut
{
public:
    CTxOutCoin()
    {
        SetNull();
    }

    CTxOutCoin(const CTxOut& txOut): CTxOut(txOut.nValue, txOut.scriptPubKey) { assetInfo = txOut.assetInfo;}
    friend bool operator==(const CTxOutCoin& a, const CTxOutCoin& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey &&
                a.assetInfo == b.assetInfo);
    }
    friend bool operator!=(const CTxOutCoin& a, const CTxOutCoin& b)
    {
        return !(a == b);
    }
    std::string ToString() const;
};
struct CMutableTransaction;

/**
 * Basic transaction serialization format:
 * - int32_t nVersion
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - uint32_t nLockTime
 *
 * Extended transaction serialization format:
 * - int32_t nVersion
 * - unsigned char dummy = 0x00
 * - unsigned char flags (!= 0)
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - if (flags & 1):
 *   - CTxWitness wit;
 * - uint32_t nLockTime
 */
template<typename Stream, typename TxType>
inline void UnserializeTransaction(TxType& tx, Stream& s) {
    const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);

    s >> tx.nVersion;
    unsigned char flags = 0;
    tx.vin.clear();
    tx.vout.clear();
    /* Try to read the vin. In case the dummy is there, this will be read as an empty vector. */
    s >> tx.vin;
    if (tx.vin.size() == 0 && fAllowWitness) {
        /* We read a dummy or an empty vin. */
        s >> flags;
        if (flags != 0) {
            s >> tx.vin;
            s >> tx.vout;
        }
    } else {
        /* We read a non-empty vin. Assume a normal vout follows. */
        s >> tx.vout;
    }
    if ((flags & 1) && fAllowWitness) {
        /* The witness flag is present, and we support witnesses. */
        flags ^= 1;
        for (size_t i = 0; i < tx.vin.size(); i++) {
            s >> tx.vin[i].scriptWitness.stack;
        }
        if (!tx.HasWitness()) {
            /* It's illegal to encode witnesses when all witness stacks are empty. */
            throw std::ios_base::failure("Superfluous witness record");
        }
    }
    if (flags) {
        /* Unknown flag in the serialization */
        throw std::ios_base::failure("Unknown transaction optional data");
    }
    s >> tx.nLockTime;
    // SYSCOIN
    tx.LoadAssets();
}

template<typename Stream, typename TxType>
inline void SerializeTransaction(const TxType& tx, Stream& s) {
    const bool fAllowWitness = !(s.GetVersion() & SERIALIZE_TRANSACTION_NO_WITNESS);

    s << tx.nVersion;
    unsigned char flags = 0;
    // Consistency check
    if (fAllowWitness) {
        /* Check whether witnesses need to be serialized. */
        if (tx.HasWitness()) {
            flags |= 1;
        }
    }
    if (flags) {
        /* Use extended format in case witnesses are to be serialized. */
        std::vector<CTxIn> vinDummy;
        s << vinDummy;
        s << flags;
    }
    s << tx.vin;
    s << tx.vout;
    if (flags & 1) {
        for (size_t i = 0; i < tx.vin.size(); i++) {
            s << tx.vin[i].scriptWitness.stack;
        }
    }
    s << tx.nLockTime;
}
// SYSCOIN
class CAssetOut {
public:
    uint32_t n;
    CAmount nValue;

    template<typename Stream>
    void Serialize(Stream &s) const;

    template<typename Stream>
    void Unserialize(Stream &s);

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
/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
public:
    // Default transaction version.
    static const int32_t CURRENT_VERSION=2;

    // Changing the default transaction version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION=2;
    // SYSCOIN consensus is driven by version, the highest version is SYSCOIN_TX_VERSION_ALLOCATION_SEND(135)
    static const int32_t MAX_SYSCOIN_STANDARD_VERSION=135;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const std::vector<CTxIn> vin;
    const std::vector<CTxOut> vout;
    const int32_t nVersion;
    const uint32_t nLockTime;
    // SYSCOIN
    const assetOutputType voutAssets;

private:
    /** Memory only. */
    const uint256 hash;
    const uint256 m_witness_hash;

    uint256 ComputeHash() const;
    uint256 ComputeWitnessHash() const;

public:
    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    explicit CTransaction(const CMutableTransaction &tx);
    CTransaction(CMutableTransaction &&tx);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeTransaction(*this, s);
    }
    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template <typename Stream>
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s)) {}

    bool IsNull() const {
        return vin.empty() && vout.empty();
    }

    const uint256& GetHash() const { return hash; }
    const uint256& GetWitnessHash() const { return m_witness_hash; };

    // Return sum of txouts.
    CAmount GetValueOut() const;
    // SYSCOIN
    bool GetAssetValueOut(const bool &isAssetTx, std::unordered_map<int32_t, CAmount> &mapAssetOut, TxValidationState& state) const;
    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsCoinBase() const
    {
        return (vin.size() == 1 && vin[0].prevout.IsNull());
    }

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.hash != b.hash;
    }

    std::string ToString() const;

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (!vin[i].scriptWitness.IsNull()) {
                return true;
            }
        }
        return false;
    }
    // SYSCOIN
    bool HasAssets() const;
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    std::vector<CTxIn> vin;
    std::vector<CTxOut> vout;
    int32_t nVersion;
    uint32_t nLockTime;
    // SYSCOIN
    assetOutputType voutAssets;

    CMutableTransaction();
    explicit CMutableTransaction(const CTransaction& tx);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeTransaction(*this, s);
    }


    template <typename Stream>
    inline void Unserialize(Stream& s) {
        UnserializeTransaction(*this, s);
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (!vin[i].scriptWitness.IsNull()) {
                return true;
            }
        }
        return false;
    }
    // SYSCOIN
    bool HasAssets() const;
    void LoadAssets();
    // from vouts, store assetInfo in voutAssets
    inline void LoadAssetsFromVout() 
    {
        voutAssets.clear();
        for(unsigned int i = 0; i< vout.size(); i++) {
            const CTxOut& txOut = vout[i];
            if(!txOut.assetInfo.IsNull()) {
                voutAssets[txOut.assetInfo.nAsset].push_back(CAssetOut(i, txOut.assetInfo.nValue));
            }
        }      
    }
};

typedef std::shared_ptr<const CTransaction> CTransactionRef;
static inline CTransactionRef MakeTransactionRef() { return std::make_shared<const CTransaction>(); }
template <typename Tx> static inline CTransactionRef MakeTransactionRef(Tx&& txIn) { return std::make_shared<const CTransaction>(std::forward<Tx>(txIn)); }
// SYSCOIN
const int SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN = 128;
const int SYSCOIN_TX_VERSION_SYSCOIN_BURN_TO_ALLOCATION = 129;
const int SYSCOIN_TX_VERSION_ASSET_ACTIVATE = 130;
const int SYSCOIN_TX_VERSION_ASSET_UPDATE = 131;
const int SYSCOIN_TX_VERSION_ASSET_SEND = 132;
const int SYSCOIN_TX_VERSION_ALLOCATION_MINT = 133;
const int SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_ETHEREUM = 134;
const int SYSCOIN_TX_VERSION_ALLOCATION_SEND = 135;

const int SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN_LEGACY = 0x7400;
enum {
	ZDAG_NOT_FOUND = -1,
	ZDAG_STATUS_OK = 0,
	ZDAG_WARNING_RBF,
    ZDAG_WARNING_NOT_ZDAG_TX,
    ZDAG_WARNING_SIZE_OVER_POLICY,
	ZDAG_MAJOR_CONFLICT
};
class CAssetAllocation {
public:
    assetOutputType voutAssets;
    
    template<typename Stream>
    void Serialize(Stream &s) const;

    template<typename Stream>
    void Unserialize(Stream &s);

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
	void SerializeData(std::vector<unsigned char>& vchData);
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

    template<typename Stream>
    void Serialize(Stream &s) const;

    template<typename Stream>
    void Unserialize(Stream &s);

    inline void SetNull() { assetAllocation.SetNull(); vchTxRoot.clear(); vchTxValue.clear(); vchTxParentNodes.clear(); vchTxPath.clear(); vchReceiptRoot.clear(); vchReceiptValue.clear(); vchReceiptParentNodes.clear(); vchReceiptPath.clear(); nBridgeTransferID = 0; nBlockNumber = 0;  }
    inline bool IsNull() const { return (vchTxValue.empty() && vchReceiptValue.empty()); }
    bool UnserializeFromData(const std::vector<unsigned char> &vchData);
    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeData(std::vector<unsigned char>& vchData);
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

    template<typename Stream>
    void Serialize(Stream &s) const;

    template<typename Stream>
    void Unserialize(Stream &s);

    inline void SetNull() { assetAllocation.SetNull(); vchEthAddress.clear();  }
    inline bool IsNull() const { return (vchEthAddress.empty() && assetAllocation.IsNull()); }
    bool UnserializeFromData(const std::vector<unsigned char> &vchData);
    bool UnserializeFromTx(const CTransaction &tx);
    void SerializeData(std::vector<unsigned char>& vchData);
};
bool IsSyscoinTx(const int &nVersion);
bool IsAssetAllocationTx(const int &nVersion);
bool IsZdagTx(const int &nVersion);
bool IsSyscoinWithNoInputTx(const int &nVersion);
bool IsAssetTx(const int &nVersion);
bool IsSyscoinMintTx(const int &nVersion);
int GetSyscoinDataOutput(const CTransaction& tx);
bool GetSyscoinData(const CTransaction &tx, std::vector<unsigned char> &vchData, int& nOut);
bool GetSyscoinData(const CScript &scriptPubKey, std::vector<unsigned char> &vchData);
typedef std::unordered_map<uint32_t, uint256> EthereumMintTxMap;
typedef std::unordered_map<int32_t, CAsset > AssetMap;
#endif // SYSCOIN_PRIMITIVES_TRANSACTION_H
