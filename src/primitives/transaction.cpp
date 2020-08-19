// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <assert.h>
// SYSCOIN
#include <dbwrapper.h>
#include <consensus/validation.h>
#include <pubkey.h>
#include <script/standard.h>
#include <services/asset.h>
#include <key_io.h>
std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}
// SYSCOIN
std::string COutPoint::ToStringShort() const
{
    return strprintf("%s-%u", hash.ToString().substr(0,64), n);
}
// SYSCOIN
CTxIn::CTxIn(const COutPoint &prevoutIn, const CScript &scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(const uint256 &hashPrevTx, uint32_t nOut, const CScript &scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}
// SYSCOIN
CTxOut::CTxOut(const CAmount& nValueIn,  const CScript &scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}
std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}
// SYSCOIN
std::string CTxOutCoin::ToString() const
{
    if(assetInfo.IsNull())
        return strprintf("CTxOutCoin(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
    else
        return strprintf("CTxOutCoin(nValue=%d.%08d, scriptPubKey=%s, nAsset=%d, nAssetValue=%d.%08d)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30), assetInfo.nAsset, assetInfo.nValue / COIN, assetInfo.nValue % COIN);
}
CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nLockTime(tx.nLockTime) {}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeWitnessHash() const
{
    if (!HasWitness()) {
        return hash;
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
// SYSCOIN
CTransaction::CTransaction() : vin(), vout(), nVersion(CTransaction::CURRENT_VERSION), nLockTime(0), voutAssets(), hash{}, m_witness_hash{} {}
CTransaction::CTransaction(const CMutableTransaction& tx) : vin(tx.vin), vout(tx.vout), nVersion(tx.nVersion), nLockTime(tx.nLockTime), voutAssets(tx.voutAssets), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}
CTransaction::CTransaction(CMutableTransaction&& tx) : vin(std::move(tx.vin)), vout(std::move(tx.vout)), nVersion(tx.nVersion), nLockTime(tx.nLockTime), voutAssets(tx.voutAssets), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    bool bFirstOutput = true;
    for (const auto& tx_out : vout) {
        // SYSCOIN
        if(bFirstOutput && (nVersion == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN || nVersion == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN_LEGACY)){
            bFirstOutput = false;
            continue;
        }
        if (!MoneyRange(tx_out.nValue) || !MoneyRange(nValueOut + tx_out.nValue))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
        nValueOut += tx_out.nValue;
    }
    assert(MoneyRange(nValueOut));
    return nValueOut;
}

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, PROTOCOL_VERSION);
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime);
    for (const auto& tx_in : vin)
        str += "    " + tx_in.ToString() + "\n";
    for (const auto& tx_in : vin)
        str += "    " + tx_in.scriptWitness.ToString() + "\n";
    for (const auto& tx_out : vout)
        str += "    " + tx_out.ToString() + "\n";
    return str;
}

// SYSCOIN
/*
 * These check for scripts for which a special case with a shorter encoding is defined.
 * They are implemented separately from the CScript test, as these test for exact byte
 * sequence correspondences, and are more strict. For example, IsToPubKey also verifies
 * whether the public key is valid (as invalid ones cannot be represented in compressed
 * form).
 */

static bool IsToKeyID(const CScript& script, CKeyID &hash)
{
    if (script.size() == 25 && script[0] == OP_DUP && script[1] == OP_HASH160
                            && script[2] == 20 && script[23] == OP_EQUALVERIFY
                            && script[24] == OP_CHECKSIG) {
        memcpy(&hash, &script[3], 20);
        return true;
    }
    return false;
}

static bool IsToScriptID(const CScript& script, CScriptID &hash)
{
    if (script.size() == 23 && script[0] == OP_HASH160 && script[1] == 20
                            && script[22] == OP_EQUAL) {
        memcpy(&hash, &script[2], 20);
        return true;
    }
    return false;
}

static bool IsToPubKey(const CScript& script, CPubKey &pubkey)
{
    if (script.size() == 35 && script[0] == 33 && script[34] == OP_CHECKSIG
                            && (script[1] == 0x02 || script[1] == 0x03)) {
        pubkey.Set(&script[1], &script[34]);
        return true;
    }
    if (script.size() == 67 && script[0] == 65 && script[66] == OP_CHECKSIG
                            && script[1] == 0x04) {
        pubkey.Set(&script[1], &script[66]);
        return pubkey.IsFullyValid(); // if not fully valid, a case that would not be compressible
    }
    return false;
}

bool CompressScript(const CScript& script, std::vector<unsigned char> &out)
{
    CKeyID keyID;
    if (IsToKeyID(script, keyID)) {
        out.resize(21);
        out[0] = 0x00;
        memcpy(&out[1], &keyID, 20);
        return true;
    }
    CScriptID scriptID;
    if (IsToScriptID(script, scriptID)) {
        out.resize(21);
        out[0] = 0x01;
        memcpy(&out[1], &scriptID, 20);
        return true;
    }
    CPubKey pubkey;
    if (IsToPubKey(script, pubkey)) {
        out.resize(33);
        memcpy(&out[1], &pubkey[1], 32);
        if (pubkey[0] == 0x02 || pubkey[0] == 0x03) {
            out[0] = pubkey[0];
            return true;
        } else if (pubkey[0] == 0x04) {
            out[0] = 0x04 | (pubkey[64] & 0x01);
            return true;
        }
    }
    return false;
}

unsigned int GetSpecialScriptSize(unsigned int nSize)
{
    if (nSize == 0 || nSize == 1)
        return 20;
    if (nSize == 2 || nSize == 3 || nSize == 4 || nSize == 5)
        return 32;
    return 0;
}

bool DecompressScript(CScript& script, unsigned int nSize, const std::vector<unsigned char> &in)
{
    switch(nSize) {
    case 0x00:
        script.resize(25);
        script[0] = OP_DUP;
        script[1] = OP_HASH160;
        script[2] = 20;
        memcpy(&script[3], in.data(), 20);
        script[23] = OP_EQUALVERIFY;
        script[24] = OP_CHECKSIG;
        return true;
    case 0x01:
        script.resize(23);
        script[0] = OP_HASH160;
        script[1] = 20;
        memcpy(&script[2], in.data(), 20);
        script[22] = OP_EQUAL;
        return true;
    case 0x02:
    case 0x03:
        script.resize(35);
        script[0] = 33;
        script[1] = nSize;
        memcpy(&script[2], in.data(), 32);
        script[34] = OP_CHECKSIG;
        return true;
    case 0x04:
    case 0x05:
        unsigned char vch[33] = {};
        vch[0] = nSize - 2;
        memcpy(&vch[1], in.data(), 32);
        CPubKey pubkey(&vch[0], &vch[33]);
        if (!pubkey.Decompress())
            return false;
        assert(pubkey.size() == 65);
        script.resize(67);
        script[0] = 65;
        memcpy(&script[1], pubkey.begin(), 65);
        script[66] = OP_CHECKSIG;
        return true;
    }
    return false;
}

// Amount compression:
// * If the amount is 0, output 0
// * first, divide the amount (in base units) by the largest power of 10 possible; call the exponent e (e is max 9)
// * if e<9, the last digit of the resulting number cannot be 0; store it as d, and drop it (divide by 10)
//   * call the result n
//   * output 1 + 10*(9*n + d - 1) + e
// * if e==9, we only know the resulting number is not zero, so output 1 + 10*(n - 1) + 9
// (this is decodable, as d is in [1-9] and e is in [0-9])

uint64_t CompressAmount(uint64_t n)
{
    if (n == 0)
        return 0;
    int e = 0;
    while (((n % 10) == 0) && e < 9) {
        n /= 10;
        e++;
    }
    if (e < 9) {
        int d = (n % 10);
        assert(d >= 1 && d <= 9);
        n /= 10;
        return 1 + (n*9 + d - 1)*10 + e;
    } else {
        return 1 + (n - 1)*10 + 9;
    }
}

uint64_t DecompressAmount(uint64_t x)
{
    // x = 0  OR  x = 1+10*(9*n + d - 1) + e  OR  x = 1+10*(n - 1) + 9
    if (x == 0)
        return 0;
    x--;
    // x = 10*(9*n + d - 1) + e
    int e = x % 10;
    x /= 10;
    uint64_t n = 0;
    if (e < 9) {
        // x = 9*n + d - 1
        int d = (x % 9) + 1;
        x /= 9;
        // x = n
        n = x*10 + d;
    } else {
        n = x+1;
    }
    while (e) {
        n *= 10;
        e--;
    }
    return n;
}

bool CTransaction::HasAssets() const
{
    return IsSyscoinTx(nVersion);
}

bool CMutableTransaction::HasAssets() const
{
    return IsSyscoinTx(nVersion);
}
bool CTransaction::IsMnTx() const
{
    return IsMasternodeTx(nVersion);
}
bool CMutableTransaction::IsMnTx() const
{
    return IsMasternodeTx(nVersion);
}
void CMutableTransaction::LoadAssets()
{
    if(HasAssets()) {
        CAssetAllocation allocation(*MakeTransactionRef(*this));
        if(allocation.IsNull()) {
            throw std::ios_base::failure("Unknown asset data");
        }
        voutAssets = std::move(allocation.voutAssets);
        if(voutAssets.empty()) {
            throw std::ios_base::failure("asset empty map");
        }
        const size_t &nVoutSize = vout.size();
        for(const auto &it: voutAssets) {
            const uint32_t &nAsset = it.key;
            if(it.values.empty()) {
                throw std::ios_base::failure("asset empty outputs");
            }
            for(const auto& voutAsset: it.values) {
                const uint32_t& nOut = voutAsset.n;
                if(nOut >= nVoutSize) {
                    throw std::ios_base::failure("asset vout out of range");
                }
                if(voutAsset.nValue > MAX_ASSET || voutAsset.nValue < 0) {
                    throw std::ios_base::failure("asset vout value out of range");
                }
                // store in vout
                CAssetCoinInfo& coinInfo = vout[nOut].assetInfo;
                coinInfo.nAsset = nAsset;
                coinInfo.nValue = voutAsset.nValue;
            }
        }       
    }
}
CAmount CTransaction::GetAssetValueOut(const std::vector<CAssetOutValue> &vecVout) const
{
    CAmount nTotal = 0;
    for(const auto& voutAsset: vecVout) {
        nTotal += voutAsset.nValue;
    }
    return nTotal;
}
bool CTransaction::GetAssetValueOut(std::unordered_map<uint32_t, std::pair<bool, CAmount> > &mapAssetOut, TxValidationState& state) const
{
    std::unordered_set<uint32_t> setUsedIndex;
    for(const auto &it: voutAssets) {
        CAmount nTotal = 0;
        if(it.values.empty()) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-empty");
        }
        const uint32_t &nAsset = it.key;
        const size_t &nVoutSize = vout.size();
        bool zeroVal = false;
        for(const auto& voutAsset: it.values) {
            const uint32_t& nOut = voutAsset.n;
            if(nOut >= nVoutSize) {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-outofrange");
            }
            const CAmount& nAmount = voutAsset.nValue;
            // make sure the vout assetinfo matches the asset commitment in OP_RETURN
            if(vout[nOut].assetInfo.nAsset != nAsset || vout[nOut].assetInfo.nValue != nAmount) {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-out-assetinfo-mismatch");
            }
            nTotal += nAmount;
            if(nAmount == 0) {
                // only one zero val per asset is allowed
                if(zeroVal) {
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-multiple-zero-out");
                }
                zeroVal = true;
            }
            if(!MoneyRangeAsset(nTotal) || !MoneyRangeAsset(nAmount)) {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-out-outofrange");
            }
            auto itSet = setUsedIndex.emplace(nOut);
            if(!itSet.second) {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-out-not-unique");
            }
        }
        auto itRes = mapAssetOut.emplace(nAsset, std::make_pair(zeroVal, nTotal));
        if(!itRes.second) {
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-asset-not-unique");
        }
    }
    return true;
}

uint256 CTransaction::GetNotarySigHash(const CAssetOut &vecOut) const {
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    for(const auto &vinObj: vin) {
        ss << vinObj.prevout;
    }
    CTxDestination txDest;
    ss << vecOut.key;
    for(const auto& voutAsset: vecOut.values){
        if (ExtractDestination(vout[voutAsset.n].scriptPubKey, txDest)) {
            ss << EncodeDestination(txDest);
            ss << voutAsset.nValue;
        }
    }
    return ss.GetHash();
}

bool IsSyscoinMintTx(const int &nVersion) {
    return nVersion == SYSCOIN_TX_VERSION_ALLOCATION_MINT;
}

bool IsAssetTx(const int &nVersion) {
    return nVersion == SYSCOIN_TX_VERSION_ASSET_ACTIVATE || nVersion == SYSCOIN_TX_VERSION_ASSET_UPDATE || nVersion == SYSCOIN_TX_VERSION_ASSET_SEND;
}

bool IsAssetAllocationTx(const int &nVersion) {
    return nVersion == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_ETHEREUM || nVersion == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN || nVersion == SYSCOIN_TX_VERSION_SYSCOIN_BURN_TO_ALLOCATION ||
        nVersion == SYSCOIN_TX_VERSION_ALLOCATION_SEND;
}

bool IsZdagTx(const int &nVersion) {
    return nVersion == SYSCOIN_TX_VERSION_ALLOCATION_SEND;
}

bool IsSyscoinTx(const int &nVersion) {
    return IsAssetTx(nVersion) || IsAssetAllocationTx(nVersion) || IsSyscoinMintTx(nVersion);
}
bool IsMasternodeTx(const int &nVersion) {
    return nVersion == SYSCOIN_TX_VERSION_MN_COINBASE ||
     nVersion == SYSCOIN_TX_VERSION_MN_QUORUM_COMMITMENT ||
     nVersion == SYSCOIN_TX_VERSION_MN_REGISTER ||
     nVersion == SYSCOIN_TX_VERSION_MN_UPDATE_SERVICE || 
     nVersion == SYSCOIN_TX_VERSION_MN_UPDATE_REGISTRAR ||
     nVersion == SYSCOIN_TX_VERSION_MN_UPDATE_REVOKE;
}
bool IsSyscoinWithNoInputTx(const int &nVersion) {
    return nVersion == SYSCOIN_TX_VERSION_ASSET_SEND || nVersion == SYSCOIN_TX_VERSION_ALLOCATION_MINT || nVersion == SYSCOIN_TX_VERSION_ASSET_ACTIVATE || nVersion == SYSCOIN_TX_VERSION_SYSCOIN_BURN_TO_ALLOCATION;
}

int GetSyscoinDataOutput(const CTransaction& tx) {
	for (unsigned int i = 0; i<tx.vout.size(); i++) {
		if (tx.vout[i].scriptPubKey.IsUnspendable())
			return i;
	}
	return -1;
}

bool GetSyscoinData(const CTransaction &tx, std::vector<unsigned char> &vchData, int& nOut)
{
	nOut = GetSyscoinDataOutput(tx);
	if (nOut == -1)
		return false;

	const CScript &scriptPubKey = tx.vout[nOut].scriptPubKey;
	return GetSyscoinData(scriptPubKey, vchData);
}

bool GetSyscoinData(const CScript &scriptPubKey, std::vector<unsigned char> &vchData)
{
	CScript::const_iterator pc = scriptPubKey.begin();
	opcodetype opcode;
	if (!scriptPubKey.GetOp(pc, opcode))
		return false;
	if (opcode != OP_RETURN)
		return false;
	if (!scriptPubKey.GetOp(pc, opcode, vchData))
		return false;
    // if witness script we get the next element which should be our MN data
    if(vchData[0] == 0xaa &&
        vchData[1] == 0x21 &&
        vchData[2] == 0xa9 &&
        vchData[3] == 0xed &&
        vchData.size() >= 36) {
        if (!scriptPubKey.GetOp(pc, opcode, vchData))
		    return false;
    }
    const unsigned int & nSize = scriptPubKey.size();
    // allow up to 80 bytes of data after our stack on standard asset transactions
    unsigned int nDifferenceAllowed = 83;
    // if data is more than 1 byte we used 2 bytes to store the varint (good enough for 64kb which is within limit of opreturn data on sys tx's)
    if(nSize >= 0xff){
        nDifferenceAllowed++;
    }
    if(nSize > (vchData.size() + nDifferenceAllowed)){
        return false;
    }
	return true;
}

bool CAssetAllocation::UnserializeFromData(const std::vector<unsigned char> &vchData) {
    try {
        CDataStream dsAsset(vchData, SER_NETWORK, PROTOCOL_VERSION);
        Unserialize(dsAsset);
    } catch (std::exception &e) {
		SetNull();
        return false;
    }
	return true;
}
bool CAssetAllocation::UnserializeFromTx(const CTransaction &tx) {
	std::vector<unsigned char> vchData;
	int nOut;
    if (!GetSyscoinData(tx, vchData, nOut))
    {
        SetNull();
        return false;
    }
    if(!UnserializeFromData(vchData))
    {	
        SetNull();
        return false;
    }
    
    return true;
}
void CAssetAllocation::SerializeData( std::vector<unsigned char> &vchData) {
    CDataStream dsAsset(SER_NETWORK, PROTOCOL_VERSION);
    Serialize(dsAsset);
	vchData = std::vector<unsigned char>(dsAsset.begin(), dsAsset.end());

}
bool CMintSyscoin::UnserializeFromData(const std::vector<unsigned char> &vchData) {
    try {
        CDataStream dsMS(vchData, SER_NETWORK, PROTOCOL_VERSION);
        Unserialize(dsMS);
    } catch (std::exception &e) {
        SetNull();
        return false;
    }
    return true;
}

bool CMintSyscoin::UnserializeFromTx(const CTransaction &tx) {
    std::vector<unsigned char> vchData;
    int nOut;
    if (!GetSyscoinData(tx, vchData, nOut))
    {
        SetNull();
        return false;
    }
    if(!UnserializeFromData(vchData))
    {   
        SetNull();
        return false;
    }  
    return true;
}

void CMintSyscoin::SerializeData( std::vector<unsigned char> &vchData) {
    CDataStream dsMint(SER_NETWORK, PROTOCOL_VERSION);
    Serialize(dsMint);
    vchData = std::vector<unsigned char>(dsMint.begin(), dsMint.end());
}

bool CBurnSyscoin::UnserializeFromData(const std::vector<unsigned char> &vchData) {
    try {
        CDataStream dsMS(vchData, SER_NETWORK, PROTOCOL_VERSION);
        Unserialize(dsMS);
    } catch (std::exception &e) {
        SetNull();
        return false;
    }
    return true;
}

bool CBurnSyscoin::UnserializeFromTx(const CTransaction &tx) {
    std::vector<unsigned char> vchData;
    int nOut;
    if (!GetSyscoinData(tx, vchData, nOut))
    {
        SetNull();
        return false;
    }
    if(!UnserializeFromData(vchData))
    {   
        SetNull();
        return false;
    }
    return true;
}

void CBurnSyscoin::SerializeData( std::vector<unsigned char> &vchData) {
    CDataStream dsBurn(SER_NETWORK, PROTOCOL_VERSION);
    Serialize(dsBurn);
    vchData = std::vector<unsigned char>(dsBurn.begin(), dsBurn.end());
}