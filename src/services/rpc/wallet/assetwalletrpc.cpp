﻿// Copyright (c) 2013-2019 The Syscoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <validation.h>
#include <services/rpc/wallet/assetwalletrpc.h>
#include <boost/algorithm/string.hpp>
#include <rpc/util.h>
#include <rpc/blockchain.h>
#include <wallet/rpcwallet.h>
#include <wallet/fees.h>
#include <policy/policy.h>
#include <consensus/validation.h>
#include <wallet/coincontrol.h>
#include <rpc/server.h>
#include <chainparams.h>
#include <util/moneystr.h>
#include <util/fees.h>
#include <util/translation.h>
#include <core_io.h>
#include <services/asset.h>
#include <node/transaction.h>
#include <rpc/auxpow_miner.h>
#include <curl/curl.h>
#include <messagesigner.h>
extern std::string EncodeDestination(const CTxDestination& dest);
extern CTxDestination DecodeDestination(const std::string& str);
uint32_t nCustomAssetGuid = 0;
CAmount getAuxFee(const CAuxFeeDetails &auxFeeDetails, const CAmount& nAmount) {
    CAmount nAccumulatedFee = 0;
    CAmount nBoundAmount = 0;
    CAmount nNextBoundAmount = 0;
    double nRate = 0;
    for(unsigned int i =0;i<auxFeeDetails.vecAuxFees.size();i++){
        const CAuxFee &fee = auxFeeDetails.vecAuxFees[i];
        const CAuxFee &feeNext = auxFeeDetails.vecAuxFees[i < auxFeeDetails.vecAuxFees.size()-1? i+1:i];  
        nBoundAmount = fee.nBound;
        nNextBoundAmount = feeNext.nBound;
        // max uint16 (65535 = 0.65535 = 65.5535%)
        nRate = fee.nPercent / 100000;
        // case where amount is in between the bounds
        if(nAmount >= nBoundAmount && nAmount < nNextBoundAmount){
            break;    
        }
        nBoundAmount = nNextBoundAmount - nBoundAmount;
        // must be last bound
        if(nBoundAmount <= 0){
            return (nAmount - nNextBoundAmount) * nRate + nAccumulatedFee;
        }
        nAccumulatedFee += (nBoundAmount * nRate);
    }
    return (nAmount - nBoundAmount) * nRate + nAccumulatedFee;    
}
struct MemoryStruct {
  char *memory;
  size_t size;
};
 
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
  char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
  if(!ptr) {
    /* out of memory! */ 
    LogPrint(BCLog::SYS, "not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;
}
 
char* curl_fetch_url(CURL *curl, const char *url, const char* payload, std::string& strError)
{
  CURLcode res;
  struct MemoryStruct chunk;
  struct curl_slist *headers = NULL;                      /* http headers to send with request */
  chunk.memory = (char*)malloc(1);  /* will be grown as needed by realloc above */ 
  chunk.size = 0;    /* no data at this point */ 
 
  if(curl) {
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 1);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    /* send all data to this function  */ 
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
 
    /* we pass our 'chunk' struct to the callback function */ 
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
 
    /* some servers don't like requests that are made without a user-agent
       field, so we provide one */ 
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
 
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
 
    /* if we don't provide POSTFIELDSIZE, libcurl will strlen() by
       itself */ 
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
 
    /* Perform the request, res will get the return code */ 
    res = curl_easy_perform(curl);
    /* Check for errors */ 
    if(res != CURLE_OK) {
      strError = strprintf("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
      return nullptr;
    } 
    curl_slist_free_all(headers);
  }
  return chunk.memory;
}
bool FillNotarySigFromEndpoint(const CTransactionRef& tx, std::vector<CAssetOut> & voutAssets, std::string& strError) {
    CURLcode resInit = curl_global_init(CURL_GLOBAL_ALL);
    if(resInit != 0) {
        strError = strprintf("curl_global_init() failed: %s\n", curl_easy_strerror(resInit));
        return false;
    }

    CURL *curl = curl_easy_init();
    std::string strHex = EncodeHexTx(*tx);
    UniValue reqObj(UniValue::VOBJ);
    reqObj.pushKV("tx", strHex); 
    std::string reqJSON = reqObj.write();
    bool bFilled = false;
    // fill notary signatures for assets that require them
    for(auto& vecOut: voutAssets) {
        // get asset
        CAsset theAsset;
        // if asset has notary signature requirement set
        if(GetAsset(vecOut.key, theAsset) && !theAsset.vchNotaryKeyID.empty()) {
            bFilled = false;
            if(!theAsset.notaryDetails.strEndPoint.empty()) {
                const std::string &strEndPoint = DecodeBase64(theAsset.notaryDetails.strEndPoint);
                char* response = curl_fetch_url(curl, strEndPoint.c_str(), reqJSON.c_str(), strError);
                if(response != nullptr) {
                    UniValue resObj;
                    if(resObj.read((const char*)response)) {
                        const UniValue &sigObj = find_value(resObj, "sig");  
                        if(sigObj.isStr()) {
                            // get signature from end-point
                            vecOut.vchNotarySig = ParseHex(sigObj.get_str());
                            // ensure sig is 65 bytes exactly for ECDSA
                            if(vecOut.vchNotarySig.size() == 65)
                                bFilled = true;
                            else {
                                strError = strprintf("Invalid signature size %d (required 65)\n", vecOut.vchNotarySig.size());
                            }
                        } else {
                            strError = "Cannot find signature field in JSON response from endpoint";
                        }
                    } else {
                        strError = "Cannot read response from endpoint";
                    }
                    free(response);
                }
            }
            if(!bFilled)
                break;
        }
    }
    if(curl)
        curl_easy_cleanup(curl);
    curl_global_cleanup();
    return bFilled;
}

bool UpdateNotarySignatureFromEndpoint(CMutableTransaction& mtx, std::string& strError) {
    const CTransactionRef& tx = MakeTransactionRef(mtx);
    std::vector<unsigned char> data;
    bool bFilledNotarySig = false;
     // call API endpoint or notary signatures and fill them in for every asset
    if(IsSyscoinMintTx(tx->nVersion)) {
        CMintSyscoin mintSyscoin(*tx);
        if(FillNotarySigFromEndpoint(tx, mintSyscoin.voutAssets, strError)) {
            bFilledNotarySig = true;
            mintSyscoin.SerializeData(data);
        }
    } else if(tx->nVersion == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_ETHEREUM) {
        CBurnSyscoin burnSyscoin(*tx);
        if(FillNotarySigFromEndpoint(tx, burnSyscoin.voutAssets, strError)) {
            bFilledNotarySig = true;
            burnSyscoin.SerializeData(data);
        }
    } else if(IsAssetAllocationTx(tx->nVersion)) {
        CAssetAllocation allocation(*tx);
        if(FillNotarySigFromEndpoint(tx, allocation.voutAssets, strError)) {
            bFilledNotarySig = true;
            allocation.SerializeData(data);
        }
    }
    if(bFilledNotarySig) {
        // find previous commitment (OP_RETURN) and replace script
        CScript scriptDataNew;
        scriptDataNew << OP_RETURN << data;
        for(auto& vout: mtx.vout) {
            if(vout.scriptPubKey.IsUnspendable()) {
                vout.scriptPubKey = scriptDataNew;
                return true;
            }
        }
    }
    return false;
}
void CreateFeeRecipient(CScript& scriptPubKey, CRecipient& recipient) {
    CRecipient recp = { scriptPubKey, 0, false };
    recipient = recp;
}

bool ListTransactionSyscoinInfo(const CWalletTx& wtx, const CAssetCoinInfo assetInfo, const std::string strCategory, UniValue& output) {
    bool found = false;
    if(IsSyscoinMintTx(wtx.tx->nVersion)) {
        found = AssetMintWtxToJson(wtx, assetInfo, strCategory, output);
    }
    else if (IsAssetTx(wtx.tx->nVersion) || IsAssetAllocationTx(wtx.tx->nVersion)) {
        found = SysWtxToJSON(wtx, assetInfo, strCategory, output);
    }
    return found;
}

bool SysWtxToJSON(const CWalletTx& wtx, const CAssetCoinInfo &assetInfo, const std::string &strCategory, UniValue& output) {
    bool found = false;
    if (IsAssetTx(wtx.tx->nVersion) && wtx.tx->nVersion != SYSCOIN_TX_VERSION_ASSET_SEND)
        found = AssetWtxToJSON(wtx, assetInfo, strCategory, output);
    else if (IsAssetAllocationTx(wtx.tx->nVersion) || wtx.tx->nVersion == SYSCOIN_TX_VERSION_ASSET_SEND)
        found = AssetAllocationWtxToJSON(wtx, assetInfo, strCategory, output);
    return found;
}

bool AssetWtxToJSON(const CWalletTx &wtx, const CAssetCoinInfo &assetInfo, const std::string &strCategory, UniValue &entry) {
    if(!AllocationWtxToJson(wtx, assetInfo, strCategory, entry))
        return false;
    CAsset asset(*wtx.tx);
    if (!asset.IsNull()) {
        if (wtx.tx->nVersion == SYSCOIN_TX_VERSION_ASSET_ACTIVATE) {
            entry.__pushKV("symbol", DecodeBase64(asset.strSymbol));
            entry.__pushKV("max_supply", asset.nMaxSupply);
            entry.__pushKV("precision", asset.nPrecision);
        }

        if(asset.nUpdateMask & ASSET_UPDATE_DATA) 
            entry.__pushKV("public_value", DecodeBase64(asset.strPubData));

        if(asset.nUpdateMask & ASSET_UPDATE_CONTRACT) 
            entry.__pushKV("contract", "0x" + HexStr(asset.vchContract));
        
        if(asset.nUpdateMask & ASSET_UPDATE_NOTARY_KEY) 
            entry.__pushKV("notary_address", EncodeDestination(WitnessV0KeyHash(uint160{asset.vchNotaryKeyID})));

        if(asset.nUpdateMask & ASSET_UPDATE_AUXFEE_KEY) 
            entry.__pushKV("auxfee_address", EncodeDestination(WitnessV0KeyHash(uint160{asset.vchAuxFeeKeyID})));

        if(asset.nUpdateMask & ASSET_UPDATE_AUXFEE_DETAILS) 
            entry.__pushKV("auxfee_details", asset.auxFeeDetails.ToJson());

        if(asset.nUpdateMask & ASSET_UPDATE_NOTARY_DETAILS) 
            entry.__pushKV("notary_details", asset.notaryDetails.ToJson());

        if(asset.nUpdateMask & ASSET_UPDATE_CAPABILITYFLAGS) 
            entry.__pushKV("updatecapability_flags", asset.nUpdateCapabilityFlags);

        if(asset.nUpdateMask & ASSET_UPDATE_SUPPLY) 
            entry.__pushKV("balance", asset.nBalance);

        entry.__pushKV("update_flags", asset.nUpdateMask);
    }
    return true;
}


bool AssetAllocationWtxToJSON(const CWalletTx &wtx, const CAssetCoinInfo &assetInfo, const std::string &strCategory, UniValue &entry) {
    if(!AllocationWtxToJson(wtx, assetInfo, strCategory, entry))
        return false;
    if(wtx.tx->nVersion == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_ETHEREUM){
         CBurnSyscoin burnSyscoin (*wtx.tx);
         if (!burnSyscoin.IsNull()) {
            CAsset dbAsset;
            GetAsset(assetInfo.nAsset, dbAsset);
            entry.__pushKV("ethereum_destination", "0x" + HexStr(burnSyscoin.vchEthAddress));
            entry.__pushKV("ethereum_contract", "0x" + HexStr(dbAsset.vchContract));
            return true;
         }
         return false;
    }
    return true;
}
bool AssetMintWtxToJson(const CWalletTx &wtx, const CAssetCoinInfo &assetInfo, const std::string &strCategory, UniValue &entry) {
    if(!AllocationWtxToJson(wtx, assetInfo, strCategory, entry))
        return false;
    CMintSyscoin mintSyscoin(*wtx.tx);
    if (!mintSyscoin.IsNull()) {
        UniValue oSPVProofObj(UniValue::VOBJ);
        oSPVProofObj.__pushKV("bridgetransferid", mintSyscoin.nBridgeTransferID);  
        std::vector<unsigned char> vchTxValue;
        if(mintSyscoin.vchTxValue.size() == 2) {
            const uint16_t &posTx = (static_cast<uint16_t>(mintSyscoin.vchTxValue[1])) | (static_cast<uint16_t>(mintSyscoin.vchTxValue[0]) << 8);
            vchTxValue = std::vector<unsigned char>(mintSyscoin.vchTxParentNodes.begin()+posTx, mintSyscoin.vchTxParentNodes.end());
        }
        else {
            vchTxValue = mintSyscoin.vchTxValue;
        }  
        oSPVProofObj.__pushKV("txvalue", HexStr(vchTxValue));   
        oSPVProofObj.__pushKV("txparentnodes", HexStr(mintSyscoin.vchTxParentNodes)); 
        oSPVProofObj.__pushKV("txpath", HexStr(mintSyscoin.vchTxPath));
        std::vector<unsigned char> vchReceiptValue;
        if(mintSyscoin.vchReceiptValue.size() == 2) {
            const uint16_t &posReceipt = (static_cast<uint16_t>(mintSyscoin.vchReceiptValue[1])) | (static_cast<uint16_t>(mintSyscoin.vchReceiptValue[0]) << 8);
            vchReceiptValue = std::vector<unsigned char>(mintSyscoin.vchReceiptParentNodes.begin()+posReceipt, mintSyscoin.vchReceiptParentNodes.end());
        }
        else{
            vchReceiptValue = mintSyscoin.vchReceiptValue;
        } 
        oSPVProofObj.__pushKV("receiptvalue", HexStr(vchReceiptValue));   
        oSPVProofObj.__pushKV("receiptparentnodes", HexStr(mintSyscoin.vchReceiptParentNodes)); 
        oSPVProofObj.__pushKV("ethblocknumber", mintSyscoin.nBlockNumber); 
        entry.__pushKV("spv_proof", oSPVProofObj); 
        UniValue oAssetAllocationReceiversArray(UniValue::VARR);
        for(const auto &it: mintSyscoin.voutAssets) {
            CAmount nTotal = 0;
            UniValue oAssetAllocationReceiversObj(UniValue::VOBJ);
            const uint32_t &nAsset = it.key;
            oAssetAllocationReceiversObj.__pushKV("asset_guid", nAsset);
            UniValue oAssetAllocationReceiverOutputsArray(UniValue::VARR);
            for(const auto& voutAsset: it.values){
                nTotal += voutAsset.nValue;
                UniValue oAssetAllocationReceiverOutputObj(UniValue::VOBJ);
                oAssetAllocationReceiverOutputObj.__pushKV("n", voutAsset.n);
                oAssetAllocationReceiverOutputObj.__pushKV("amount", voutAsset.nValue);
                oAssetAllocationReceiverOutputsArray.push_back(oAssetAllocationReceiverOutputObj);
            }
            oAssetAllocationReceiversObj.__pushKV("outputs", oAssetAllocationReceiverOutputsArray); 
            oAssetAllocationReceiversObj.__pushKV("total", nTotal);
            oAssetAllocationReceiversArray.push_back(oAssetAllocationReceiversObj);
        }
        entry.__pushKV("allocations", oAssetAllocationReceiversArray);
    }
    return true;
}

bool AllocationWtxToJson(const CWalletTx &wtx, const CAssetCoinInfo &assetInfo, const std::string &strCategory, UniValue &entry) {
    entry.__pushKV("txtype", stringFromSyscoinTx(wtx.tx->nVersion));
    entry.__pushKV("asset_guid", assetInfo.nAsset);
    if(IsAssetAllocationTx(wtx.tx->nVersion)) {
        entry.__pushKV("amount", assetInfo.nValue);
        entry.__pushKV("action", strCategory);
    }
    return true;
}

void TestTransaction(const CTransactionRef& tx, const util::Ref& context) {
    if(!fAssetIndex) { 
        throw JSONRPCError(RPC_WALLET_ERROR, "missing-asset-index");
    }
    AssertLockHeld(cs_main);
    CTxMemPool& mempool = EnsureMemPool(context);
    int64_t virtual_size = GetVirtualTransactionSize(*tx);
    CAmount max_raw_tx_fee = DEFAULT_MAX_RAW_TX_FEE_RATE.GetFee(virtual_size);

    TxValidationState state;
    bool test_accept_res = AcceptToMemoryPool(mempool, state, tx,
            nullptr /* plTxnReplaced */, false /* bypass_limits */, max_raw_tx_fee, /* test_accept */ true);

    if (!test_accept_res) {
        if (state.IsInvalid()) {
            if (state.GetResult() == TxValidationResult::TX_MISSING_INPUTS) {
                throw JSONRPCError(RPC_WALLET_ERROR, "missing-inputs");
            } else {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s", state.ToString()));
            }
        } else {
            throw JSONRPCError(RPC_WALLET_ERROR, state.ToString());
        }
    }
}
UniValue signhash(const JSONRPCRequest& request)
{
        RPCHelpMan{"signhash",
                "\nSign a hash with the private key of an address" +
        HELP_REQUIRING_PASSPHRASE,
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The syscoin address to use for the private key."},
                    {"hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hash to create a signature of."},
                },
                RPCResult{
                    RPCResult::Type::STR, "signature", "The signature of the message encoded in base 64"
                },
                RPCExamples{
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signhash", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\" \"hash\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("signhash", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XX\", \"hash\"")
                },
            }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    const CWallet* const pwallet = wallet.get();

    LegacyScriptPubKeyMan& spk_man = EnsureLegacyScriptPubKeyMan(*wallet);

    LOCK2(pwallet->cs_wallet, spk_man.cs_KeyStore);

    EnsureWalletIsUnlocked(pwallet);

    std::string strAddress = request.params[0].get_str();
    uint256 hash = ParseHashV(request.params[1], "hash");

    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    auto keyid = GetKeyForDestination(spk_man, dest);
    if (keyid.IsNull()) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    }
    CKey vchSecret;
    if (!spk_man.GetKey(keyid, vchSecret)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    }
    std::vector<unsigned char> vchSig;
    if(!CHashSigner::SignHash(hash, vchSecret, vchSig)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "SignHash failed");
    }
   
    if (!CHashSigner::VerifyHash(hash, vchSecret.GetPubKey(), vchSig)) {
        LogPrintf("CSporkMessage::Sign -- VerifyHash() failed\n");
        return false;
    }
    return EncodeBase64(vchSig.data(), vchSig.size());
}
UniValue syscoinburntoassetallocation(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"syscoinburntoassetallocation",
        "\nBurns Syscoin to the SYSX asset\n",
        {
            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid of SYSX"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount of SYS to burn."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            }},
        RPCExamples{
            HelpExampleCli("syscoinburntoassetallocation", "\"asset_guid\" \"amount\"")
            + HelpExampleRpc("syscoinburntoassetallocation", "\"asset_guid\", \"amount\"")
        }
    }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);
    const uint32_t &nAsset = params[0].get_uint();          	
	CAssetAllocation theAssetAllocation;
	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }

    // Parse the label first so we don't generate a key if there's an error
    std::string label = "";
    CTxDestination dest;
    std::string errorStr;
    if (!pwallet->GetNewDestination(pwallet->m_default_address_type, label, dest, errorStr)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, errorStr);
    }

    const CScript& scriptPubKey = GetScriptForDestination(dest);
    CTxOut change_prototype_txout(0, scriptPubKey);
    CRecipient recp = {scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };


    CMutableTransaction mtx;
    CAmount nAmount;
    try{
        nAmount = AssetAmountFromValue(params[1], theAsset.nPrecision);
    }
    catch(...) {
        nAmount = params[1].get_int64();
    }

    std::vector<CAssetOutValue> outVec = {CAssetOutValue(1, nAmount)};
    theAssetAllocation.voutAssets.emplace_back(CAssetOut(nAsset, outVec));


    std::vector<unsigned char> data;
    theAssetAllocation.SerializeData(data); 
    
    CScript scriptData;
    scriptData << OP_RETURN << data;  
    CRecipient burn;
    CreateFeeRecipient(scriptData, burn);
    burn.nAmount = nAmount;
    std::vector<CRecipient> vecSend;
    vecSend.push_back(burn);
    vecSend.push_back(recp);
    mtx.nVersion = SYSCOIN_TX_VERSION_SYSCOIN_BURN_TO_ALLOCATION;
    CCoinControl coin_control;
    int nChangePosRet = -1;
    bilingual_str error;
    CAmount nFeeRequired = 0;
    CAmount curBalance = pwallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted;
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    if (!pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, error, coin_control)) {
        if (tx->GetValueOut() + nFeeRequired > curBalance)
            error = strprintf(Untranslated("Error: This transaction requires a transaction fee of at least %s"), FormatMoney(nFeeRequired));
    }
    TestTransaction(tx, request.context);
    mapValue_t mapValue;
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    UniValue res(UniValue::VOBJ);
    res.__pushKV("txid", tx->GetHash().GetHex());
    return res;
}
UniValue assetnew(const JSONRPCRequest& request) {
    uint32_t nCustomGuid = nCustomAssetGuid;
    if(nCustomAssetGuid > 0)
        nCustomAssetGuid = 0;
    const UniValue &params = request.params;
    RPCHelpMan{"assetnew",
    "\nCreate a new asset\n",
    {
        {"funding_amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Fund resulting UTXO owning the asset by this much SYS for gas."},
        {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO, "Asset symbol (1-8 characters)"},
        {"description", RPCArg::Type::STR, RPCArg::Optional::NO, "Public description of the token."},
        {"contract", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Ethereum token contract for SyscoinX bridge. Must be in hex and not include the '0x' format tag. For example contract '0xb060ddb93707d2bc2f8bcc39451a5a28852f8d1d' should be set as 'b060ddb93707d2bc2f8bcc39451a5a28852f8d1d'. Leave empty for no smart contract bridge."},
        {"precision", RPCArg::Type::NUM, RPCArg::Optional::NO, "Precision of balances. Must be between 0 and 8. The lower it is the higher possible max_supply is available since the supply is represented as a 64 bit integer. With a precision of 8 the max supply is 10 billion."},
        {"total_supply", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Initial supply of asset. Can mint more supply up to total_supply amount."},
        {"max_supply", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Maximum supply of this asset. Depends on the precision value that is set, the lower the precision the higher max_supply can be."},
        {"updatecapability_flags", RPCArg::Type::NUM, RPCArg::Optional::NO, "Ability to update certain fields. Must be decimal value which is a bitmask for certain rights to update. The bitmask represents 1 to give admin status (needed to update flags), 2 for updating public data field, 4 for updating the smart contract field, 8 for updating supply, 16 for updating witness, 32 for being able to update flags (need admin access to update flags as well). 63 for all."},
        {"notary_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Notary address"},
        {"auxfee_address", RPCArg::Type::STR, RPCArg::Optional::NO, "AuxFee address"},
        {"notary_details", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Notary details structure (if notary_address is set)",
            {
                {"endpoint", RPCArg::Type::STR, RPCArg::Optional::NO, "Notary API endpoint (if applicable)"},
                {"instant_transfers", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Enforced double-spend prevention on Notary for Instant Transfers"},
                {"hd_required", RPCArg::Type::BOOL, RPCArg::Optional::NO, "If Notary requires HD Wallet approval (for sender approval specifically applicable to change address schemes), usually in the form of account XPUB or Verifiable Credential of account XPUB using DID"},  
            }
        },
        {"auxfee_details", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Auxiliary fee structure (may be enforced if notary and auxfee_address is set)",
            {
                {"fee_struct", RPCArg::Type::ARR, RPCArg::Optional::NO, "Auxiliary fee structure",
                    {
                        {"", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "Bound (in amount) for for the fee level based on total transaction amount"},
                        {"", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Percentage of total transaction amount applied as a fee"},
                    },
                }
            }
        }

    },
    RPCResult{
        RPCResult::Type::OBJ, "", "",
        {
            {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            {RPCResult::Type::NUM, "asset_guid", "The unique identifier of the new asset"}
        }},
    RPCExamples{
    HelpExampleCli("assetnew", "1 \"CAT\" \"publicvalue\" \"contractaddr\" 8 100 1000 63 \"notary_address\" \"auxfee_address\" {} {}")
    + HelpExampleRpc("assetnew", "1, \"CAT\", \"publicvalue\", \"contractaddr\", 8, 100, 1000, 63, \"notary_address\", \"auxfee_address\", {}, {}")
    }
    }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();  
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);
    CAmount nGas;
    std::string strSymbol = params[1].get_str();
    std::string strPubData = params[2].get_str();
    if(strPubData == "''")
        strPubData.clear();
    std::string strContract = params[3].get_str();
    if(strContract == "''")
        strContract.clear();
    if(!strContract.empty())
         boost::erase_all(strContract, "0x");  // strip 0x in hex str if exist

    uint32_t precision = params[4].get_uint();
    UniValue param0 = params[0];
    try{
        nGas = AmountFromValue(param0);
    }
    catch(...) {
        nGas = 0;
    }
    CAmount nBalance;
    try{
        nBalance = AssetAmountFromValue(params[5], precision);
    }
    catch(...) {
        nBalance = params[5].get_int64();
    }
    CAmount nMaxSupply;
    try{
        nMaxSupply = AssetAmountFromValue(params[6], precision);
    }
    catch(...) {
        nMaxSupply = params[6].get_int64();
    }
    uint32_t nUpdateCapabilityFlags = params[7].get_uint();
    std::string strNotary = params[8].get_str();
    std::vector<unsigned char> vchNotaryKeyID;
    if(!strNotary.empty()) {
        CTxDestination txDest = DecodeDestination(strNotary);
        if (!IsValidDestination(txDest)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid notary address");
        }
        if (auto witness_id = boost::get<WitnessV0KeyHash>(&txDest)) {	
            CKeyID keyID = ToKeyID(*witness_id);
            vchNotaryKeyID = std::vector<unsigned char>(keyID.begin(), keyID.end());
        } else {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid notary address: Please use P2PWKH address.");
        }
    }
    std::string strAuxFee = params[9].get_str();
    std::vector<unsigned char> vchAuxFeeKeyID;
    if(!strAuxFee.empty()) {
        CTxDestination txDest = DecodeDestination(strAuxFee);
        if (!IsValidDestination(txDest)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid auxfee address");
        }
        if (auto witness_id = boost::get<WitnessV0KeyHash>(&txDest)) {	
            CKeyID keyID = ToKeyID(*witness_id);
            vchAuxFeeKeyID = std::vector<unsigned char>(keyID.begin(), keyID.end());
        } else {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid auxfee address: Please use P2PWKH address.");
        }
    }
    CNotaryDetails notaryDetails(params[10]);
    CAuxFeeDetails auxFeeDetails(params[11], precision);
    // calculate net
    // build asset object
    CAsset newAsset;

    UniValue publicData(UniValue::VOBJ);
    publicData.pushKV("desc", EncodeBase64(strPubData));
    uint8_t nUpdateMask = ASSET_UPDATE_SUPPLY | ASSET_UPDATE_CAPABILITYFLAGS;
    const std::string &strPubDataField  = publicData.write();
    std::vector<CAssetOutValue> outVec = {CAssetOutValue(0, 0)};
    newAsset.voutAssets.emplace_back(CAssetOut(0, outVec));
    newAsset.strSymbol = EncodeBase64(strSymbol);
    if(!strPubDataField.empty()) {
        nUpdateMask |= ASSET_UPDATE_DATA;
        newAsset.strPubData = strPubDataField;
    }
    if(!strContract.empty()) {
        nUpdateMask |= ASSET_UPDATE_CONTRACT;
        newAsset.vchContract = ParseHex(strContract);
    }
    if(!vchNotaryKeyID.empty()) {
        nUpdateMask |= ASSET_UPDATE_NOTARY_KEY;
        newAsset.vchNotaryKeyID = vchNotaryKeyID;
    }
    if(!vchAuxFeeKeyID.empty()) {
        nUpdateMask |= ASSET_UPDATE_AUXFEE_KEY;
        newAsset.vchAuxFeeKeyID = vchAuxFeeKeyID;
    }
    if(!notaryDetails.IsNull()) {
        nUpdateMask |= ASSET_UPDATE_NOTARY_DETAILS;
        newAsset.notaryDetails = notaryDetails;
    }
    if(!auxFeeDetails.IsNull()) {
        nUpdateMask |= ASSET_UPDATE_AUXFEE_DETAILS;
        newAsset.auxFeeDetails = auxFeeDetails;
    }
    newAsset.nUpdateMask = nUpdateMask;
    newAsset.nBalance = nBalance;
    newAsset.nMaxSupply = nMaxSupply;
    newAsset.nPrecision = precision;
    newAsset.nUpdateCapabilityFlags = nUpdateCapabilityFlags;
    newAsset.nTotalSupply = 0;
    newAsset.nPrevUpdateCapabilityFlags = nUpdateCapabilityFlags;
    std::vector<unsigned char> data;
    newAsset.SerializeData(data);
    // use the script pub key to create the vecsend which sendmoney takes and puts it into vout
    std::vector<CRecipient> vecSend;

    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }
    // Parse the label first so we don't generate a key if there's an error
    std::string label = "";
    CTxDestination dest;
    std::string errorStr;
    if (!pwallet->GetNewDestination(pwallet->m_default_address_type, label, dest, errorStr)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, errorStr);
    }
    CMutableTransaction mtx;
    std::set<int> setSubtractFeeFromOutputs;
    // new/send/update all have asset utxo's with 0 asset amount
    const CScript& scriptPubKey = GetScriptForDestination(dest);
    CTxOut change_prototype_txout(nGas, scriptPubKey);
    bool isDust = nGas < COIN;
    CRecipient recp = { scriptPubKey, isDust? GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)): nGas,  !isDust};
    mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
    if(nGas > 0)
        setSubtractFeeFromOutputs.insert(0);
    CScript scriptData;
    scriptData << OP_RETURN << data;
    CRecipient opreturnRecipient;
    CreateFeeRecipient(scriptData, opreturnRecipient);
    // 150 SYS fee for new asset
    opreturnRecipient.nAmount = 150*COIN;
    
    mtx.vout.push_back(CTxOut(opreturnRecipient.nAmount, opreturnRecipient.scriptPubKey));
    CAmount nFeeRequired = 0;
    bilingual_str error;
    int nChangePosRet = -1;
    CCoinControl coin_control;
    // assetnew must not be replaceable
    coin_control.m_signal_bip125_rbf = false;
    bool lockUnspents = false;   
    mtx.nVersion = SYSCOIN_TX_VERSION_ASSET_ACTIVATE;
    if (!pwallet->FundTransaction(mtx, nFeeRequired, nChangePosRet, error, lockUnspents, setSubtractFeeFromOutputs, coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
    data.clear();
    // generate deterministic guid based on input txid
    const uint32_t &nAsset = nCustomGuid != 0? nCustomGuid: GenerateSyscoinGuid(mtx.vin[0].prevout);
    newAsset.voutAssets.clear();
    newAsset.voutAssets.emplace_back(CAssetOut(nAsset, outVec));
    newAsset.SerializeData(data);
    scriptData.clear();
    scriptData << OP_RETURN << data;
    CreateFeeRecipient(scriptData, opreturnRecipient);
    // 150 SYS fee for new asset
    opreturnRecipient.nAmount = 150*COIN;
    mtx.vout.clear();
    mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
    mtx.vout.push_back(CTxOut(opreturnRecipient.nAmount, opreturnRecipient.scriptPubKey));
    nFeeRequired = 0;
    nChangePosRet = -1;
    if (!pwallet->FundTransaction(mtx, nFeeRequired, nChangePosRet, error, lockUnspents, setSubtractFeeFromOutputs, coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
    if(!pwallet->SignTransaction(mtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign transaction");
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    TestTransaction(tx, request.context);
    mapValue_t mapValue;
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    UniValue res(UniValue::VOBJ);
    res.__pushKV("txid", tx->GetHash().GetHex());
    res.__pushKV("asset_guid", nAsset);
    return res;
}
UniValue assetnewtest(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assetnewtest",
    "\nCreate a new asset with a specific GUID. Useful for testing purposes.\n",
    {
        {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Create asset with this GUID. Only on regtest."},
        {"funding_amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Fund resulting UTXO owning the asset by this much SYS for gas."},
        {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO, "Asset symbol (1-8 characters)"},
        {"description", RPCArg::Type::STR, RPCArg::Optional::NO, "Public description of the token."},
        {"contract", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Ethereum token contract for SyscoinX bridge. Must be in hex and not include the '0x' format tag. For example contract '0xb060ddb93707d2bc2f8bcc39451a5a28852f8d1d' should be set as 'b060ddb93707d2bc2f8bcc39451a5a28852f8d1d'. Leave empty for no smart contract bridge."},
        {"precision", RPCArg::Type::NUM, RPCArg::Optional::NO, "Precision of balances. Must be between 0 and 8. The lower it is the higher possible max_supply is available since the supply is represented as a 64 bit integer. With a precision of 8 the max supply is 10 billion."},
        {"total_supply", RPCArg::Type::NUM, RPCArg::Optional::NO, "Initial supply of asset. Can mint more supply up to total_supply amount."},
        {"max_supply", RPCArg::Type::NUM, RPCArg::Optional::NO, "Maximum supply of this asset. Depends on the precision value that is set, the lower the precision the higher max_supply can be."},
        {"updatecapability_flags", RPCArg::Type::NUM, RPCArg::Optional::NO, "Ability to update certain fields. Must be decimal value which is a bitmask for certain rights to update. The bitmask represents 1 to give admin status (needed to update flags), 2 for updating public data field, 4 for updating the smart contract field, 8 for updating supply, 16 for updating witness, 32 for being able to update flags (need admin access to update flags as well). 63 for all."},
        {"notary_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Notary address"},
        {"auxfee_address", RPCArg::Type::STR, RPCArg::Optional::NO, "AuxFee address"},
        {"notary_details", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Notary details structure (if notary_address is set)",
            {
                {"endpoint", RPCArg::Type::STR, RPCArg::Optional::NO, "Notary API endpoint (if applicable)"},
                {"instant_transfers", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Enforced double-spend prevention on Notary for Instant Transfers"},
                {"hd_required", RPCArg::Type::BOOL, RPCArg::Optional::NO, "If Notary requires HD Wallet approval (for sender approval specifically applicable to change address schemes), usually in the form of account XPUB or Verifiable Credential of account XPUB using DID"},  
            }
        },
        {"auxfee_details", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Auxiliary fee structure (may be enforced if notary and auxfee_address is set)",
            {
                {"fee_struct", RPCArg::Type::ARR, RPCArg::Optional::NO, "Auxiliary fee structure",
                    {
                        {"", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "Bound (in amount) for for the fee level based on total transaction amount"},
                        {"", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Percentage of total transaction amount applied as a fee"},
                    },
                }
            }
        }
    },
    RPCResult{
        RPCResult::Type::OBJ, "", "",
        {
            {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            {RPCResult::Type::NUM, "asset_guid", "The unique identifier of the new asset"}
        }},
    RPCExamples{
    HelpExampleCli("assetnewtest", "1234 1 \"CAT\" \"publicvalue\" \"contractaddr\" 8 100 1000 63 \"notary_address\" \"auxfee_address\" {} {}")
    + HelpExampleRpc("assetnewtest", "1234 1, \"CAT\", \"publicvalue\", \"contractaddr\", 8, 100, 1000, 63, \"notary_address\", \"auxfee_address\", {}, {}")
    }
    }.Check(request);
    UniValue paramsFund(UniValue::VARR);
    nCustomAssetGuid = params[0].get_uint();
    for(int i = 1;i<=12;i++)
        paramsFund.push_back(params[i]);
    JSONRPCRequest assetNewRequest(request.context);
    assetNewRequest.params = paramsFund;
    assetNewRequest.URI = request.URI;
    return assetnew(assetNewRequest);        
}
UniValue CreateAssetUpdateTx(const util::Ref& context, const int32_t& nVersionIn, const uint32_t &nAsset, CWallet* const pwallet, std::vector<CRecipient>& vecSend, const CRecipient& opreturnRecipient,const CRecipient* recpIn = nullptr) {
    AssertLockHeld(pwallet->cs_wallet);
    CCoinControl coin_control;
    CAmount nMinimumAmountAsset = 0;
    CAmount nMaximumAmountAsset = 0;
    CAmount nMinimumSumAmountAsset = 0;
    coin_control.assetInfo = CAssetCoinInfo(nAsset, nMaximumAmountAsset);
    std::vector<COutput> vecOutputs;
    pwallet->AvailableCoins(vecOutputs, true, &coin_control, 0, MAX_MONEY, 0, nMinimumAmountAsset, nMaximumAmountAsset, nMinimumSumAmountAsset);
    int nNumOutputsFound = 0;
    int nFoundOutput = -1;
    for(unsigned int i = 0; i < vecOutputs.size(); i++) {
        if(!vecOutputs[i].fSpendable || !vecOutputs[i].fSolvable)
            continue;
        nNumOutputsFound++;
        nFoundOutput = i;
    }
    if(nNumOutputsFound > 1) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Too many inputs found for this asset, should only have exactly one input");
    }
    if(nNumOutputsFound <= 0) {
        throw JSONRPCError(RPC_WALLET_ERROR, "No inputs found for this asset");
    }
    
    if (!pwallet->CanGetAddresses()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: This wallet has no available keys");
    }
    const CInputCoin &inputCoin = vecOutputs[nFoundOutput].GetInputCoin();
    const CAmount &nGas = inputCoin.effective_value;  
    // subtract fee from this output (it should pay the gas which was funded by asset new)
    CRecipient recp = { CScript(), 0, false };
    if(recpIn) {
        vecSend.push_back(*recpIn);
    }
    if(!recpIn || nGas > (MIN_CHANGE + pwallet->m_default_max_tx_fee)) {
        // Parse the label first so we don't generate a key if there's an error
        std::string label = "";
        CTxDestination dest;
        std::string errorStr;
        if (!pwallet->GetNewDestination(pwallet->m_default_address_type, label, dest, errorStr)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, errorStr);
        }
        recp = { GetScriptForDestination(dest), nGas, false };  
    }
    // if enough for change + max fee, we try to take fee from this output
    if(nGas > (MIN_CHANGE + pwallet->m_default_max_tx_fee)) {
        recp.fSubtractFeeFromAmount = true;
        CAmount nTotalOther = 0;
        // deduct other sys amounts from this output which will pay the outputs and fees
        for(const auto& recipient: vecSend) {
            nTotalOther += recipient.nAmount;
        }
        // if adding other outputs would make this output not have enough to pay the fee, don't sub fee from amount
        if(nTotalOther >= (nGas - (MIN_CHANGE + pwallet->m_default_max_tx_fee)))
            recp.fSubtractFeeFromAmount = false;
        else
            recp.nAmount -= nTotalOther;
    }
    CMutableTransaction mtx;
    // order matters here as vecSend is in sync with asset commitment, it may change later when
    // change is added but it will resync the commitment there
    if(recp.nAmount > 0)
        vecSend.push_back(recp);
    vecSend.push_back(opreturnRecipient);
    CAmount nFeeRequired = 0;
    bilingual_str error;
    int nChangePosRet = -1;
    coin_control.Select(inputCoin.outpoint);
    coin_control.fAllowOtherInputs = recp.nAmount <= 0 || !recp.fSubtractFeeFromAmount; // select asset + sys utxo's
    CAmount curBalance = pwallet->GetBalance(0, coin_control.m_avoid_address_reuse).m_mine_trusted;
    mtx.nVersion = nVersionIn;
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    if (!pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, error, coin_control)) {
        if (tx->GetValueOut() + nFeeRequired > curBalance)
            error = strprintf(Untranslated("Error: This transaction requires a transaction fee of at least %s"), FormatMoney(nFeeRequired));
    }
    TestTransaction(tx, context);
    mapValue_t mapValue;
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    UniValue res(UniValue::VOBJ);
    res.__pushKV("txid", tx->GetHash().GetHex());
    return res;
}

UniValue assetupdate(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assetupdate",
        "\nPerform an update on an asset you control.\n",
        {
            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid"},
            {"description", RPCArg::Type::STR, RPCArg::Optional::NO, "Public description of the token."},
            {"contract",  RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Ethereum token contract for SyscoinX bridge. Leave empty for no smart contract bridge."},
            {"supply", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "New supply of asset. Can mint more supply up to total_supply amount or if max_supply is -1 then minting is uncapped. If greater than zero, minting is assumed otherwise set to 0 to not mint any additional tokens."},
            {"updatecapability_flags", RPCArg::Type::NUM, RPCArg::Optional::NO, "Ability to update certain fields. Must be decimal value which is a bitmask for certain rights to update. The bitmask represents 1 to give admin status (needed to update flags), 2 for updating public data field, 4 for updating the smart contract field, 8 for updating supply, 16 for updating witness, 32 for being able to update flags (need admin access to update flags as well). 63 for all."},
            {"notary_address", RPCArg::Type::STR, RPCArg::Optional::NO, "Notary address"},
            {"auxfee_address", RPCArg::Type::STR, RPCArg::Optional::NO, "AuxFee address"},
            {"notary_details", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Notary details structure (if notary_address is set)",
                {
                    {"endpoint", RPCArg::Type::STR, RPCArg::Optional::NO, "Notary API endpoint (if applicable)"},
                    {"instant_transfers", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Enforced double-spend prevention on Notary for Instant Transfers"},
                    {"hd_required", RPCArg::Type::BOOL, RPCArg::Optional::NO, "If Notary requires HD Wallet approval (for sender approval specifically applicable to change address schemes), usually in the form of account XPUB or Verifiable Credential of account XPUB using DID"},  
                }
            },
            {"auxfee_details", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Auxiliary fee structure (may be enforced if notary and auxfee_address is set)",
                {
                    {"fee_struct", RPCArg::Type::ARR, RPCArg::Optional::NO, "Auxiliary fee structure",
                        {
                            {"", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "Bound (in amount) for for the fee level based on total transaction amount"},
                            {"", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Percentage of total transaction amount applied as a fee"},
                        },
                    }
                }
            }
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"}
            }},
        RPCExamples{
            HelpExampleCli("assetupdate", "\"asset_guid\" \"description\" \"contract\" \"supply\" \"update_flags\" \"notary_address\" \"auxfee_address\" {} {}")
            + HelpExampleRpc("assetupdate", "\"asset_guid\", \"description\", \"contract\", \"supply\", \"update_flags\", \"notary_address\", \"auxfee_address\", {}, {}")
        }
        }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);    
    EnsureWalletIsUnlocked(pwallet);
    const uint32_t &nAsset = params[0].get_uint();
    std::string strData = "";
    std::string strCategory = "";
    std::string strPubData = params[1].get_str();
    if(strPubData == "''")
        strPubData.clear();
    std::string strContract = params[2].get_str();
    if(strContract == "''")
        strContract.clear();
    if(!strContract.empty())
        boost::erase_all(strContract, "0x");  // strip 0x if exist
    std::vector<unsigned char> vchContract = ParseHex(strContract);
    uint8_t nUpdateCapabilityFlags = (uint8_t)params[4].get_uint();
    
    CAsset theAsset;

    if (!GetAsset( nAsset, theAsset))
        throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");
        
    const std::string oldData = theAsset.strPubData;
    const std::vector<unsigned char> oldContract(theAsset.vchContract);
    const std::vector<unsigned char> vchOldNotaryKeyID(theAsset.vchNotaryKeyID);
    const std::vector<unsigned char> vchOldAuxFeeKeyID(theAsset.vchAuxFeeKeyID);
    const CNotaryDetails oldNotaryDetails = theAsset.notaryDetails;
    const CAuxFeeDetails oldAuxFeeDetails = theAsset.auxFeeDetails;
    const uint8_t nOldUpdateCapabilityFlags = theAsset.nUpdateCapabilityFlags;
    theAsset.ClearAsset();
    CAmount nBalance;
    try{
        nBalance = AssetAmountFromValue(params[3], theAsset.nPrecision);
    }
    catch(...) {
        nBalance = params[3].get_int64();
    }
    UniValue publicData(UniValue::VOBJ);
    publicData.pushKV("desc", EncodeBase64(strPubData));
    std::string strNotary = params[5].get_str();
    std::vector<unsigned char> vchNotaryKeyID;
    if(!strNotary.empty()) {
        CTxDestination txDest = DecodeDestination(strNotary);
        if (!IsValidDestination(txDest)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid notary address");
        }
        if (auto witness_id = boost::get<WitnessV0KeyHash>(&txDest)) {	
            CKeyID keyID = ToKeyID(*witness_id);
            vchNotaryKeyID = std::vector<unsigned char>(keyID.begin(), keyID.end());
        } else {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid notary address: Please use P2PWKH address.");
        }
    }
    std::string strAuxFee = params[6].get_str();
    std::vector<unsigned char> vchAuxFeeKeyID;
    if(!strAuxFee.empty()) {
        CTxDestination txDest = DecodeDestination(strAuxFee);
        if (!IsValidDestination(txDest)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid auxfee address");
        }
        if (auto witness_id = boost::get<WitnessV0KeyHash>(&txDest)) {	
            CKeyID keyID = ToKeyID(*witness_id);
            vchAuxFeeKeyID = std::vector<unsigned char>(keyID.begin(), keyID.end());
        } else {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid auxfee address: Please use P2PWKH address.");
        }
    }
    uint8_t nUpdateMask = 0;
    CNotaryDetails notaryDetails(params[7]);
    CAuxFeeDetails auxFeeDetails(params[8], theAsset.nPrecision);
    strPubData = publicData.write();
    if(strPubData != oldData) {
        nUpdateMask |= ASSET_UPDATE_DATA;
        theAsset.strPrevPubData = oldData;
        theAsset.strPubData = strPubData;
    }

    if(vchContract != oldContract) {
        nUpdateMask |= ASSET_UPDATE_CONTRACT;
        theAsset.vchPrevContract = oldContract;
        theAsset.vchContract = vchContract;
    }

    if(vchNotaryKeyID != vchOldNotaryKeyID) {
        nUpdateMask |= ASSET_UPDATE_NOTARY_KEY;
        theAsset.vchPrevNotaryKeyID = vchOldNotaryKeyID;
        theAsset.vchNotaryKeyID = vchNotaryKeyID;
    }

    if(notaryDetails != oldNotaryDetails) {
        nUpdateMask |= ASSET_UPDATE_NOTARY_DETAILS;
        theAsset.prevNotaryDetails = oldNotaryDetails;
        theAsset.notaryDetails = notaryDetails;
    }

    if(vchAuxFeeKeyID != vchOldAuxFeeKeyID) {
        nUpdateMask |= ASSET_UPDATE_AUXFEE_KEY;
        theAsset.vchPrevAuxFeeKeyID = vchOldAuxFeeKeyID;
        theAsset.vchAuxFeeKeyID = vchAuxFeeKeyID;
    }

    if(auxFeeDetails != oldAuxFeeDetails) {
        nUpdateMask |= ASSET_UPDATE_AUXFEE_DETAILS;
        theAsset.prevAuxFeeDetails = oldAuxFeeDetails;
        theAsset.auxFeeDetails = auxFeeDetails;
    }
    if(nBalance > 0) {
        nUpdateMask |= ASSET_UPDATE_SUPPLY;
        theAsset.nBalance = nBalance;
    }
    if(nUpdateCapabilityFlags != nOldUpdateCapabilityFlags) {
        nUpdateMask |= ASSET_UPDATE_CAPABILITYFLAGS;
        theAsset.nPrevUpdateCapabilityFlags = nOldUpdateCapabilityFlags;
        theAsset.nUpdateCapabilityFlags = nUpdateCapabilityFlags;
    }
    theAsset.nUpdateMask = nUpdateMask;
    std::vector<CAssetOutValue> outVec = {CAssetOutValue(0, 0)};
    theAsset.voutAssets.emplace_back(CAssetOut(nAsset, outVec));
    std::vector<unsigned char> data;
    theAsset.SerializeData(data);
    CScript scriptData;
    scriptData << OP_RETURN << data;
    CRecipient opreturnRecipient;
    CreateFeeRecipient(scriptData, opreturnRecipient);
    std::vector<CRecipient> vecSend;
    return CreateAssetUpdateTx(request.context, SYSCOIN_TX_VERSION_ASSET_UPDATE, nAsset, pwallet, vecSend, opreturnRecipient);
}

UniValue assettransfer(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assettransfer",
        "\nPerform a transfer of ownership on an asset you control.\n",
        {
            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid"},
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "New owner of asset."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            }},
        RPCExamples{
            HelpExampleCli("assettransfer", "\"asset_guid\" \"address\"")
            + HelpExampleRpc("assettransfer", "\"asset_guid\", \"address\"")
        }
        }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);    
    EnsureWalletIsUnlocked(pwallet);
    const uint32_t &nAsset = params[0].get_uint();
    std::string strAddress = params[1].get_str();
   
    CAsset theAsset;

    if (!GetAsset( nAsset, theAsset)) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");
    }
    const CScript& scriptPubKey = GetScriptForDestination(DecodeDestination(strAddress));
    CTxOut change_prototype_txout(0, scriptPubKey);
    CRecipient recp = {scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };
    theAsset.ClearAsset();
    std::vector<CAssetOutValue> outVec = {CAssetOutValue(0, 0)};
    theAsset.voutAssets.emplace_back(CAssetOut(nAsset, outVec));

    std::vector<unsigned char> data;
    theAsset.SerializeData(data);
    CScript scriptData;
    scriptData << OP_RETURN << data;
    CRecipient opreturnRecipient;
    CreateFeeRecipient(scriptData, opreturnRecipient);
    std::vector<CRecipient> vecSend;
    return CreateAssetUpdateTx(request.context, SYSCOIN_TX_VERSION_ASSET_UPDATE, nAsset, pwallet, vecSend, opreturnRecipient, &recp);
}

UniValue assetsendmany(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assetsendmany",
    "\nSend an asset you own to another address/addresses as an asset allocation. Maximum recipients is 250.\n",
    {
        {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid."},
        {"amounts", RPCArg::Type::ARR, RPCArg::Optional::NO, "Array of asset send objects.",
            {
                {"", RPCArg::Type::OBJ, RPCArg::Optional::NO, "An assetsend obj",
                    {
                        {"address", RPCArg::Type::NUM, RPCArg::Optional::NO, "Address to transfer to"},
                        {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount of asset to send"}
                    }
                }
            },
            "[assetsendobjects,...]"
        }
    },
    RPCResult{
        RPCResult::Type::OBJ, "", "",
        {
            {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
        }},
    RPCExamples{
        HelpExampleCli("assetsendmany", "\"asset_guid\" '[{\"address\":\"sysaddress1\",\"amount\":100},{\"address\":\"sysaddress2\",\"amount\":200}]\'")
        + HelpExampleCli("assetsendmany", "\"asset_guid\" \"[{\\\"address\\\":\\\"sysaddress1\\\",\\\"amount\\\":100},{\\\"address\\\":\\\"sysaddress2\\\",\\\"amount\\\":200}]\"")
        + HelpExampleRpc("assetsendmany", "\"asset_guid\",\'[{\"address\":\"sysaddress1\",\"amount\":100},{\"address\":\"sysaddress2\",\"amount\":200}]\'")
        + HelpExampleRpc("assetsendmany", "\"asset_guid\",\"[{\\\"address\\\":\\\"sysaddress1\\\",\\\"amount\\\":100},{\\\"address\\\":\\\"sysaddress2\\\",\\\"amount\\\":200}]\"")
    }
    }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    // gather & validate inputs
    const uint32_t &nAsset = params[0].get_uint();
    UniValue valueTo = params[1];
    if (!valueTo.isArray())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Array of receivers not found");

    CAsset theAsset;
    if (!GetAsset(nAsset, theAsset))
        throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");


    CAssetAllocation theAssetAllocation;
    UniValue receivers = valueTo.get_array();
    std::vector<CRecipient> vecSend;
    std::vector<CAssetOutValue> vecOut;
    for (unsigned int idx = 0; idx < receivers.size(); idx++) {
        const UniValue& receiver = receivers[idx];
        if (!receiver.isObject())
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"asset_guid\", \"address\", \"amount\"}");

        const UniValue &receiverObj = receiver.get_obj();
        const std::string &toStr = find_value(receiverObj, "address").get_str(); 
        const CScript& scriptPubKey = GetScriptForDestination(DecodeDestination(toStr));           
        CAmount nAmount = AssetAmountFromValue(find_value(receiverObj, "amount"), theAsset.nPrecision);

        auto it = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
        if(it == theAssetAllocation.voutAssets.end()) {
            theAssetAllocation.voutAssets.emplace_back(CAssetOut(nAsset, vecOut));
            it = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
        }
        const size_t len = it->values.size();
        it->values.push_back(CAssetOutValue(len, nAmount));
        CTxOut change_prototype_txout(0, scriptPubKey);
        CRecipient recp = { scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };
        vecSend.push_back(recp);
    }
    auto it = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
    if(it == theAssetAllocation.voutAssets.end()) {
        theAssetAllocation.voutAssets.emplace_back(CAssetOut(nAsset, vecOut));
        it = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
    }
    const size_t len = it->values.size();
    // add change for asset
    it->values.push_back(CAssetOutValue(len, 0));
    CScript scriptPubKey;
    std::vector<unsigned char> data;
    theAssetAllocation.SerializeData(data);

    CScript scriptData;
    scriptData << OP_RETURN << data;
    CRecipient opreturnRecipient;
    CreateFeeRecipient(scriptData, opreturnRecipient);
    return CreateAssetUpdateTx(request.context, SYSCOIN_TX_VERSION_ASSET_SEND, nAsset, pwallet, vecSend, opreturnRecipient);
}

UniValue assetsend(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assetsend",
    "\nSend an asset you own to another address.\n",
    {
        {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "The asset guid."},
        {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to send the asset to (creates an asset allocation)."},
        {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount of asset to send."}
    },
    RPCResult{
        RPCResult::Type::OBJ, "", "",
        {
            {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
        }},
    RPCExamples{
        HelpExampleCli("assetsend", "\"asset_guid\" \"address\" \"amount\"")
        + HelpExampleRpc("assetsend", "\"asset_guid\", \"address\", \"amount\"")
        }

    }.Check(request);
    const uint32_t &nAsset = params[0].get_uint();          
    UniValue output(UniValue::VARR);
    UniValue outputObj(UniValue::VOBJ);
    outputObj.__pushKV("address", params[1].get_str());
    outputObj.__pushKV("amount", request.params[2]);
    output.push_back(outputObj);
    UniValue paramsFund(UniValue::VARR);
    paramsFund.push_back(nAsset);
    paramsFund.push_back(output);
    JSONRPCRequest requestMany(request.context);
    requestMany.params = paramsFund;
    requestMany.URI = request.URI;
    return assetsendmany(requestMany);          
}

UniValue assetallocationsendmany(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
    RPCHelpMan{"assetallocationsendmany",
        "\nSend an asset allocation you own to another address. Maximum recipients is 250.\n",
        {
            {"amounts", RPCArg::Type::ARR, RPCArg::Optional::NO, "Array of assetallocationsend objects",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "The assetallocationsend object",
                        {
                            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid"},
                            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address to transfer to"},
                            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "Amount of asset to send"}
                        }
                    },
                    },
                    "[assetallocationsend object]..."
            },
            {"replaceable", RPCArg::Type::BOOL, /* default */ "wallet default", "Allow this transaction to be replaced by a transaction with higher fees via BIP 125. ZDAG is only possible if RBF is disabled."},
            {"comment", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "A comment"},
            {"conf_target", RPCArg::Type::NUM, /* default */ "wallet default", "Confirmation target (in blocks)"},
            {"estimate_mode", RPCArg::Type::STR, /* default */ "UNSET", "The fee estimate mode, must be one of:\n"
            "       \"UNSET\"\n"
            "       \"ECONOMICAL\"\n"
            "       \"CONSERVATIVE\""},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            }},
        RPCExamples{
            HelpExampleCli("assetallocationsendmany", "\'[{\"asset_guid\":1045909988,\"address\":\"sysaddress1\",\"amount\":100},{\"asset_guid\":1045909988,\"address\":\"sysaddress2\",\"amount\":200}]\' \"false\"")
            + HelpExampleCli("assetallocationsendmany", "\"[{\\\"asset_guid\\\":1045909988,\\\"address\\\":\\\"sysaddress1\\\",\\\"amount\\\":100},{\\\"asset_guid\\\":1045909988,\\\"address\\\":\\\"sysaddress2\\\",\\\"amount\\\":200}]\" \"true\"")
            + HelpExampleRpc("assetallocationsendmany", "\'[{\"asset_guid\":1045909988,\"address\":\"sysaddress1\",\"amount\":100},{\"asset_guid\":1045909988,\"address\":\"sysaddress2\",\"amount\":200}]\',\"false\"")
            + HelpExampleRpc("assetallocationsendmany", "\"[{\\\"asset_guid\\\":1045909988,\\\"address\\\":\\\"sysaddress1\\\",\\\"amount\\\":100},{\\\"asset_guid\\\":1045909988,\\\"address\\\":\\\"sysaddress2\\\",\\\"amount\\\":200}]\",\"true\"")
        }
    }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    CCoinControl coin_control;
	// gather & validate inputs
	UniValue valueTo = params[0];
	if (!valueTo.isArray())
		throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Array of receivers not found");
    bool m_signal_bip125_rbf = false;
    if (!request.params[1].isNull()) {
        m_signal_bip125_rbf = request.params[1].get_bool();
    }
    mapValue_t mapValue;
    if (!request.params[2].isNull() && !request.params[2].get_str().empty())
        mapValue["comment"] = request.params[2].get_str();
    if (!request.params[3].isNull()) {
        coin_control.m_confirm_target = ParseConfirmTarget(request.params[3], pwallet->chain().estimateMaxBlocks());
    }
    if (!request.params[4].isNull()) {
        if (!FeeModeFromString(request.params[4].get_str(), coin_control.m_fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
    }
    CAssetAllocation theAssetAllocation;
    CMutableTransaction mtx;
	UniValue receivers = valueTo.get_array();
    std::map<uint32_t, uint64_t> mapAssetTotals;
    std::vector<CAssetOutValue> vecOut;
    uint8_t bOverideRBF = 0;
	for (unsigned int idx = 0; idx < receivers.size(); idx++) {
        CAmount nTotalSending = 0;
		const UniValue& receiver = receivers[idx];
		if (!receiver.isObject())
			throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"address\" or \"amount\"}");

		const UniValue &receiverObj = receiver.get_obj();
        const uint32_t &nAsset = find_value(receiverObj, "asset_guid").get_uint();
        CAsset theAsset;
        if (!GetAsset(nAsset, theAsset))
            throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");
        // override RBF if one notarized asset has it enabled
        if(!bOverideRBF && !theAsset.vchNotaryKeyID.empty() && !theAsset.notaryDetails.IsNull()) {
            bOverideRBF = theAsset.notaryDetails.bEnableInstantTransfers;
        }

        const std::string &toStr = find_value(receiverObj, "address").get_str();
        const CScript& scriptPubKey = GetScriptForDestination(DecodeDestination(toStr));   
        CTxOut change_prototype_txout(0, scriptPubKey);
        const CAmount &nAmount = AssetAmountFromValue(find_value(receiverObj, "amount"), theAsset.nPrecision);
        auto itVout = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
        if(itVout == theAssetAllocation.voutAssets.end()) {
            CAssetOut assetOut(nAsset, vecOut);
            if(!theAsset.vchNotaryKeyID.empty()) {
                // fund tx expecting 65 byte signature to be filled in
                assetOut.vchNotarySig.resize(65);
            }
            theAssetAllocation.voutAssets.emplace_back(assetOut);
            itVout = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
        }
        itVout->values.push_back(CAssetOutValue(mtx.vout.size(), nAmount));

        CRecipient recp = { scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };
        mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
        auto it = mapAssetTotals.emplace(nAsset, nAmount);
        if(!it.second) {
            it.first->second += nAmount;
        }
        nTotalSending += nAmount;
	        
    }
    // if all instant transfers using notary, we use RBF
    if(bOverideRBF) {
        // only override if parameter was not provided by user
        if(request.params[1].isNull())
            m_signal_bip125_rbf = true;
    }
    // aux fees if applicable
    for(const auto &it: mapAssetTotals) {
        const uint32_t &nAsset = it.first;
        CAsset theAsset;
        if (!GetAsset(nAsset, theAsset))
            throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");
        const CAmount &nAuxFee = getAuxFee(theAsset.auxFeeDetails, it.second);
        if(nAuxFee > 0 && !theAsset.vchAuxFeeKeyID.empty()){
            auto itVout = std::find_if( theAssetAllocation.voutAssets.begin(), theAssetAllocation.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
            if(itVout == theAssetAllocation.voutAssets.end()) {
                 throw JSONRPCError(RPC_DATABASE_ERROR, "Invalid asset not found in voutAssets");
            }
            itVout->values.push_back(CAssetOutValue(mtx.vout.size(), nAuxFee));
            const CScript& scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(uint160{theAsset.vchAuxFeeKeyID}));
            CTxOut change_prototype_txout(0, scriptPubKey);
            CRecipient recp = {scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };
            mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
            auto it = mapAssetTotals.emplace(nAsset, nAuxFee);
            if(!it.second) {
                it.first->second += nAuxFee;
            }
        }
    }
    coin_control.m_signal_bip125_rbf = m_signal_bip125_rbf;
    EnsureWalletIsUnlocked(pwallet);

	std::vector<unsigned char> data;
	theAssetAllocation.SerializeData(data);   


	CScript scriptData;
	scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, fee);
    mtx.vout.push_back(CTxOut(fee.nAmount, fee.scriptPubKey));
    CAmount nFeeRequired = 0;
    bilingual_str error;
    int nChangePosRet = -1;
    bool lockUnspents = false;
    std::set<int> setSubtractFeeFromOutputs;


    // if zdag double the fee rate
    if(coin_control.m_signal_bip125_rbf == false) {
        coin_control.m_feerate = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE*2);
    }
    mtx.nVersion = SYSCOIN_TX_VERSION_ALLOCATION_SEND;
    for(const auto &it: mapAssetTotals) {
        nChangePosRet = -1;
        nFeeRequired = 0;
        coin_control.assetInfo = CAssetCoinInfo(it.first, it.second);
        if (!pwallet->FundTransaction(mtx, nFeeRequired, nChangePosRet, error, lockUnspents, setSubtractFeeFromOutputs, coin_control)) {
            throw JSONRPCError(RPC_WALLET_ERROR, error.original);
        }
    }
    if(!pwallet->SignTransaction(mtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign transaction");
    }
    std::string strError = "";
    if(UpdateNotarySignatureFromEndpoint(mtx, strError)) {
        if(!pwallet->SignTransaction(mtx)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign notarized transaction");
        }
    } else if(!strError.empty()) {
        UniValue res(UniValue::VOBJ);
        res.__pushKV("hex", EncodeHexTx(CTransaction(mtx)));
        res.__pushKV("error", strError);
        return res;
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    TestTransaction(tx, request.context);
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    UniValue res(UniValue::VOBJ);
    res.__pushKV("txid", tx->GetHash().GetHex());
    return res;
}
UniValue assetallocationburn(const JSONRPCRequest& request) {
	const UniValue &params = request.params;
    RPCHelpMan{"assetallocationburn",
        "\nBurn an asset allocation in order to use the bridge or move back to Syscoin\n",
        {
            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount of asset to burn to SYSX"},
            {"ethereum_destination_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The 20 byte (40 character) hex string of the ethereum destination address. Set to '' to burn to Syscoin."}
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            }},
        RPCExamples{
            HelpExampleCli("assetallocationburn", "\"asset_guid\" \"amount\" \"ethereum_destination_address\"")
            + HelpExampleRpc("assetallocationburn", "\"asset_guid\", \"amount\", \"ethereum_destination_address\"")
        }
    }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    const uint32_t &nAsset = params[0].get_uint();
    	
	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");
        
    CAmount nAmount;
    try{
        nAmount = AssetAmountFromValue(params[1], theAsset.nPrecision);
    }
    catch(...) {
        nAmount = params[1].get_int64();
    }
	std::string ethAddress = params[2].get_str();
    boost::erase_all(ethAddress, "0x");  // strip 0x if exist
    CScript scriptData;
    int32_t nVersionIn = 0;

    CBurnSyscoin burnSyscoin;
    int nChangePosRet = 1; 
    // if no eth address provided just send as a std asset allocation send but to burn address
    if(ethAddress.empty() || ethAddress == "''") {
        nVersionIn = SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN;
        std::vector<CAssetOutValue> vecOut = {CAssetOutValue(1, nAmount)}; // burn has to be in index 1, sys is output in index 0, any change in index 2
        CAssetOut assetOut(nAsset, vecOut);
        if(!theAsset.vchNotaryKeyID.empty()) {
            assetOut.vchNotarySig.resize(65);  
        }
        burnSyscoin.voutAssets.emplace_back(assetOut);
        nChangePosRet++;
    }
    else {
        burnSyscoin.vchEthAddress = ParseHex(ethAddress);
        nVersionIn = SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_ETHEREUM;
        std::vector<CAssetOutValue> vecOut = {CAssetOutValue(0, nAmount)}; // burn has to be in index 0, any change in index 1
        CAssetOut assetOut(nAsset, vecOut);
        if(!theAsset.vchNotaryKeyID.empty()) {
            assetOut.vchNotarySig.resize(65);  
        }
        burnSyscoin.voutAssets.emplace_back(assetOut);
    }

    std::string label = "";
    CTxDestination dest;
    std::string errorStr;
    if (!pwallet->GetNewDestination(pwallet->m_default_address_type, label, dest, errorStr)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, errorStr);
    }

    const CScript& scriptPubKey = GetScriptForDestination(dest);
    CRecipient recp = {scriptPubKey, nAmount, false };


    std::vector<unsigned char> data;
    burnSyscoin.SerializeData(data);  
    scriptData << OP_RETURN << data;
	CRecipient fee;
	CreateFeeRecipient(scriptData, fee);
    CMutableTransaction mtx;
    // output to new sys output
    if(nVersionIn == SYSCOIN_TX_VERSION_ALLOCATION_BURN_TO_SYSCOIN)
        mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
    // burn output
    mtx.vout.push_back(CTxOut(fee.nAmount, fee.scriptPubKey));
    CAmount nFeeRequired = 0;
    bool lockUnspents = false;
    std::set<int> setSubtractFeeFromOutputs;
    bilingual_str error;
    mtx.nVersion = nVersionIn;
    CCoinControl coin_control;
    coin_control.assetInfo = CAssetCoinInfo(nAsset, nAmount);
    if (!pwallet->FundTransaction(mtx, nFeeRequired, nChangePosRet, error, lockUnspents, setSubtractFeeFromOutputs, coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
    if(!pwallet->SignTransaction(mtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign transaction");
    }
    std::string strError = "";
    if(UpdateNotarySignatureFromEndpoint(mtx, strError)) {
        if(!pwallet->SignTransaction(mtx)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign notarized transaction");
        }
    } else if(!strError.empty()) {
        UniValue res(UniValue::VOBJ);
        res.__pushKV("hex", EncodeHexTx(CTransaction(mtx)));
        res.__pushKV("error", strError);
        return res;
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    TestTransaction(tx, request.context);
    mapValue_t mapValue;
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    UniValue res(UniValue::VOBJ);
    res.__pushKV("txid", tx->GetHash().GetHex());
    return res;
}

std::vector<unsigned char> ushortToBytes(unsigned short paramShort) {
     std::vector<unsigned char> arrayOfByte(2);
     for (int i = 0; i < 2; i++)
         arrayOfByte[1 - i] = (paramShort >> (i * 8));
     return arrayOfByte;
}

UniValue assetallocationmint(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assetallocationmint",
        "\nMint assetallocation to come back from the bridge\n",
        {
            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "Asset guid"},
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Mint to this address."},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount of asset to mint.  Note that fees (in SYS) will be taken from the owner address"},
            {"blocknumber", RPCArg::Type::NUM, RPCArg::Optional::NO, "Block number of the block that included the burn transaction on Ethereum."},
            {"bridge_transfer_id", RPCArg::Type::NUM, RPCArg::Optional::NO, "Unique Bridge Transfer ID for this event from Ethereum. It is the low 32 bits of the transferIdAndPrecisions field in the TokenFreeze Event on freezeBurnERC20 call."},
            {"tx_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Transaction hex."},
            {"txmerkleproof_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The list of parent nodes of the Merkle Patricia Tree for SPV proof of transaction merkle root."},
            {"merklerootpath_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The merkle path to walk through the tree to recreate the merkle hash for both transaction and receipt root."},
            {"receipt_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Transaction Receipt Hex."},
            {"receiptmerkleproof_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The list of parent nodes of the Merkle Patricia Tree for SPV proof of transaction receipt merkle root."},
            {"auxfee_test", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Used for internal testing only."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            }},
        RPCExamples{
            HelpExampleCli("assetallocationmint", "\"asset_guid\" \"address\" \"amount\" \"blocknumber\" \"bridge_transfer_id\" \"tx_hex\" \"txmerkleproof_hex\" \"txmerkleproofpath_hex\" \"receipt_hex\" \"receiptmerkleproof\"")
            + HelpExampleRpc("assetallocationmint", "\"asset_guid\", \"address\", \"amount\", \"blocknumber\", \"bridge_transfer_id\", \"tx_hex\", \"txmerkleproof_hex\", \"txmerkleproofpath_hex\", \"receipt_hex\", \"receiptmerkleproof\"")
        }
    }.Check(request);
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK(pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);
    const uint32_t &nAsset = params[0].get_uint();
    std::string strAddress = params[1].get_str();
	CAsset theAsset;
	if (!GetAsset(nAsset, theAsset))
		throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");    
    CAmount nAmount;
    try{
        nAmount = AssetAmountFromValue(request.params[2], theAsset.nPrecision);
    }
    catch(...) {
        nAmount = request.params[2].get_int64();
    }        
    const uint32_t &nBlockNumber = params[3].get_uint(); 
    const uint32_t &nBridgeTransferID = params[4].get_uint(); 
    
    std::string vchTxValue = params[5].get_str();
    std::string vchTxParentNodes = params[6].get_str();

    // find byte offset of tx data in the parent nodes
    size_t pos = vchTxParentNodes.find(vchTxValue);
    if(pos == std::string::npos || vchTxParentNodes.size() > (USHRT_MAX*2)){
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find tx value in tx parent nodes");  
    }
    uint16_t posTxValue = (uint16_t)pos/2;
    std::string vchTxPath = params[7].get_str();
 
    std::string vchReceiptValue = params[8].get_str();
    std::string vchReceiptParentNodes = params[9].get_str();
    pos = vchReceiptParentNodes.find(vchReceiptValue);
    if(pos == std::string::npos || vchReceiptParentNodes.size() > (USHRT_MAX*2)){
        throw JSONRPCError(RPC_TYPE_ERROR, "Could not find receipt value in receipt parent nodes");  
    }
    uint16_t posReceiptValue = (uint16_t)pos/2;
    if(!fGethSynced){
        throw JSONRPCError(RPC_MISC_ERROR, "Geth is not synced, please wait until it syncs up and try again");
    }


    std::vector<CRecipient> vecSend;
    
    CMintSyscoin mintSyscoin;
    std::vector<CAssetOutValue> vecOut = {CAssetOutValue(0, nAmount)};
    CAssetOut assetOut(nAsset, vecOut);
    if(!theAsset.vchNotaryKeyID.empty()) {
        assetOut.vchNotarySig.resize(65);
    }
    mintSyscoin.voutAssets.emplace_back(assetOut);
    mintSyscoin.nBlockNumber = nBlockNumber;
    mintSyscoin.nBridgeTransferID = nBridgeTransferID;
    mintSyscoin.vchTxValue = ushortToBytes(posTxValue);
    mintSyscoin.vchTxParentNodes = ParseHex(vchTxParentNodes);
    mintSyscoin.vchTxPath = ParseHex(vchTxPath);
    mintSyscoin.vchReceiptValue = ushortToBytes(posReceiptValue);
    mintSyscoin.vchReceiptParentNodes = ParseHex(vchReceiptParentNodes);
    
    const CScript& scriptPubKey = GetScriptForDestination(DecodeDestination(strAddress));
    CTxOut change_prototype_txout(0, scriptPubKey);
    CRecipient recp = {scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };    
    
    CMutableTransaction mtx;
    mtx.nVersion = SYSCOIN_TX_VERSION_ALLOCATION_MINT;
    mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
    if(params.size() >= 11 && params[10].isBool() && params[10].get_bool()) {
        // aux fees test
        CAsset theAsset;
        if (!GetAsset(nAsset, theAsset))
            throw JSONRPCError(RPC_DATABASE_ERROR, "Could not find a asset with this key");
        const CAmount &nAuxFee = getAuxFee(theAsset.auxFeeDetails, nAmount);
        if(nAuxFee > 0 && !theAsset.vchAuxFeeKeyID.empty()){
            auto itVout = std::find_if( mintSyscoin.voutAssets.begin(), mintSyscoin.voutAssets.end(), [&nAsset](const CAssetOut& element){ return element.key == nAsset;} );
            if(itVout == mintSyscoin.voutAssets.end()) {
                throw JSONRPCError(RPC_DATABASE_ERROR, "Invalid asset not found in voutAssets");
            }
            itVout->values.push_back(CAssetOutValue(mtx.vout.size(), nAuxFee));
            const CScript& scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(uint160{theAsset.vchAuxFeeKeyID}));
            CTxOut change_prototype_txout(0, scriptPubKey);
            CRecipient recp = {scriptPubKey, GetDustThreshold(change_prototype_txout, GetDiscardRate(*pwallet)), false };
            mtx.vout.push_back(CTxOut(recp.nAmount, recp.scriptPubKey));
        }
    }
    std::vector<unsigned char> data;
    mintSyscoin.SerializeData(data);
    CScript scriptData;
    scriptData << OP_RETURN << data;
    CRecipient fee;
    CreateFeeRecipient(scriptData, fee);
    mtx.vout.push_back(CTxOut(fee.nAmount, fee.scriptPubKey));
    CAmount nFeeRequired = 0;
    bilingual_str error;
    int nChangePosRet = -1;
    CCoinControl coin_control;
    bool lockUnspents = false;
    std::set<int> setSubtractFeeFromOutputs;
    if (!pwallet->FundTransaction(mtx, nFeeRequired, nChangePosRet, error, lockUnspents, setSubtractFeeFromOutputs, coin_control)) {
        throw JSONRPCError(RPC_WALLET_ERROR, error.original);
    }
    if(!pwallet->SignTransaction(mtx)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign transaction");
    }
    std::string strError = "";
    if(UpdateNotarySignatureFromEndpoint(mtx, strError)) {
        if(!pwallet->SignTransaction(mtx)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Could not sign notarized transaction");
        }
    } else if(!strError.empty()) {
        UniValue res(UniValue::VOBJ);
        res.__pushKV("hex", EncodeHexTx(CTransaction(mtx)));
        res.__pushKV("error", strError);
        return res;
    }
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    TestTransaction(tx, request.context);
    mapValue_t mapValue;
    pwallet->CommitTransaction(tx, std::move(mapValue), {} /* orderForm */);
    UniValue res(UniValue::VOBJ);
    res.__pushKV("txid", tx->GetHash().GetHex());
    return res;  
}

UniValue assetallocationsend(const JSONRPCRequest& request) {
    const UniValue &params = request.params;
    RPCHelpMan{"assetallocationsend",
        "\nSend an asset allocation you own to another address.\n",
        {
            {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "The asset guid"},
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to send the allocation to"},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "Amount of asset to send"},
            {"replaceable", RPCArg::Type::BOOL, /* default */ "wallet default", "Allow this transaction to be replaced by a transaction with higher fees via BIP 125. ZDAG is only possible if RBF is disabled."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
            }},
        RPCExamples{
            HelpExampleCli("assetallocationsend", "\"asset_guid\" \"address\" \"amount\" \"false\"")
            + HelpExampleRpc("assetallocationsend", "\"asset_guid\", \"address\", \"amount\" \"false\"")
        }
    }.Check(request);
    const uint32_t &nAsset = params[0].get_uint();          
    bool m_signal_bip125_rbf = false;
    if (!request.params[3].isNull()) {
        m_signal_bip125_rbf = request.params[3].get_bool();
    }  
    UniValue replaceableObj(UniValue::VBOOL);
    UniValue commentObj(UniValue::VSTR);
    UniValue confObj(UniValue::VNUM);
    UniValue feeObj(UniValue::VSTR);
    replaceableObj.setBool(m_signal_bip125_rbf);
    commentObj.setStr("");
    confObj.setInt(DEFAULT_TX_CONFIRM_TARGET);
    feeObj.setStr("UNSET");
    UniValue output(UniValue::VARR);
    UniValue outputObj(UniValue::VOBJ);
    outputObj.__pushKV("asset_guid", nAsset);
    outputObj.__pushKV("address", params[1].get_str());
    outputObj.__pushKV("amount", request.params[2]);
    output.push_back(outputObj);
    UniValue paramsFund(UniValue::VARR);
    paramsFund.push_back(output);
    paramsFund.push_back(replaceableObj);
    paramsFund.push_back(commentObj); // comment
    paramsFund.push_back(confObj); // conf_target
    paramsFund.push_back(feeObj); // estimate_mode
    JSONRPCRequest requestMany(request.context);
    requestMany.params = paramsFund;
    requestMany.URI = request.URI;
    return assetallocationsendmany(requestMany);          
}

UniValue convertaddresswallet(const JSONRPCRequest& request) {	

    RPCHelpMan{"convertaddresswallet",
    "\nConvert between Syscoin 3 and Syscoin 4 formats. This should only be used with addressed based on compressed private keys only. P2WPKH can be shown as P2PKH in Syscoin 3. Adds to wallet as receiving address under label specified.",   
    {	
        {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The syscoin address to get the information of."},	
        {"label", RPCArg::Type::STR,RPCArg::Optional::NO, "Label Syscoin V4 address and store in receiving address. Set to \"\" to not add to receiving address", "An optional label"},	
        {"rescan", RPCArg::Type::BOOL, /* default */ "false", "Rescan the wallet for transactions. Useful if you provided label to add to receiving address"},	
    },	
    RPCResult{
        RPCResult::Type::OBJ, "", "",
        {
            {RPCResult::Type::STR, "v3address", "The syscoin 3 address validated"},
            {RPCResult::Type::STR, "v4address", "The syscoin 4 address validated"},
        },
    },		
    RPCExamples{	
        HelpExampleCli("convertaddresswallet", "\"sys1qw40fdue7g7r5ugw0epzk7xy24tywncm26hu4a7\" \"bob\" true")	
        + HelpExampleRpc("convertaddresswallet", "\"sys1qw40fdue7g7r5ugw0epzk7xy24tywncm26hu4a7\" \"bob\" true")	
    }}.Check(request);	
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    UniValue ret(UniValue::VOBJ);	
    CTxDestination dest = DecodeDestination(request.params[0].get_str());	
    std::string strLabel = "";	
    if (!request.params[1].isNull())	
        strLabel = request.params[1].get_str();    	
    bool fRescan = false;	
    if (!request.params[2].isNull())	
        fRescan = request.params[2].get_bool();	
    // Make sure the destination is valid	
    if (!IsValidDestination(dest)) {	
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");	
    }	
    std::string currentV4Address = "";	
    std::string currentV3Address = "";	
    CTxDestination v4Dest;	
    if (auto witness_id = boost::get<WitnessV0KeyHash>(&dest)) {	
        v4Dest = dest;	
        currentV4Address =  EncodeDestination(v4Dest);	
        currentV3Address =  EncodeDestination(*witness_id);	
    }	
    else if (auto key_id = boost::get<PKHash>(&dest)) {	
        v4Dest = WitnessV0KeyHash(*key_id);	
        currentV4Address =  EncodeDestination(v4Dest);	
        currentV3Address =  EncodeDestination(*key_id);	
    }	
    else if (auto script_id = boost::get<ScriptHash>(&dest)) {	
        v4Dest = *script_id;	
        currentV4Address =  EncodeDestination(v4Dest);	
        currentV3Address =  currentV4Address;	
    }	
    else if (boost::get<WitnessV0ScriptHash>(&dest)) {	
        v4Dest = dest;	
        currentV4Address =  EncodeDestination(v4Dest);	
        currentV3Address =  currentV4Address;	
    } 	
    else	
        strLabel = "";	
    isminetype mine = pwallet->IsMine(v4Dest);	
    if(!(mine & ISMINE_SPENDABLE)){	
        throw JSONRPCError(RPC_MISC_ERROR, "The V4 Public key or redeemscript not known to wallet, or the key is uncompressed.");	
    }	
    if(!strLabel.empty())	
    {	
        LOCK(pwallet->cs_wallet);   	
        CScript witprog = GetScriptForDestination(v4Dest);	
        LegacyScriptPubKeyMan* spk_man = pwallet->GetLegacyScriptPubKeyMan();	
        if(spk_man)	
            spk_man->AddCScript(witprog); // Implicit for single-key now, but necessary for multisig and for compatibility	
        pwallet->SetAddressBook(v4Dest, strLabel, "receive");	
        WalletRescanReserver reserver(*pwallet);                   	
        if (fRescan) {	
            int64_t scanned_time = pwallet->RescanFromTime(0, reserver, true);	
            if (pwallet->IsAbortingRescan()) {	
                throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");	
            } else if (scanned_time > 0) {	
                throw JSONRPCError(RPC_WALLET_ERROR, "Rescan was unable to fully rescan the blockchain. Some transactions may be missing.");	
            }	
        }  	
    }	

    ret.pushKV("v3address", currentV3Address);	
    ret.pushKV("v4address", currentV4Address); 	
    return ret;	
}


UniValue listunspentasset(const JSONRPCRequest& request) {	
    RPCHelpMan{"listunspentasset",
    "\nHelper function which just calls listunspent to find unspent UTXO's for an asset.",   
    {	
        {"asset_guid", RPCArg::Type::NUM, RPCArg::Optional::NO, "The syscoin asset guid to get the information of."},	
        {"minconf", RPCArg::Type::NUM, /* default */ "1", "The minimum confirmations to filter"},	
    },	
    RPCResult{
        RPCResult::Type::STR, "result", "Result"
    },		
    RPCExamples{	
        HelpExampleCli("listunspentasset", "2328882 0")	
        + HelpExampleRpc("listunspentasset", "2328882 0")	
    }}.Check(request);	

    uint32_t nAsset = request.params[0].get_uint();
    int nMinDepth = 1;
    if (!request.params[1].isNull()) {
        nMinDepth = request.params[1].get_int();
    }
    int nMaxDepth = 9999999;
    bool include_unsafe = true;
    UniValue paramsFund(UniValue::VARR);
    UniValue addresses(UniValue::VARR);
    UniValue includeSafe(UniValue::VBOOL);
    includeSafe.setBool(include_unsafe);
    paramsFund.push_back(nMinDepth);
    paramsFund.push_back(nMaxDepth);
    paramsFund.push_back(addresses);
    paramsFund.push_back(includeSafe);
    
    UniValue options(UniValue::VOBJ);
    options.__pushKV("assetGuid", nAsset);
    paramsFund.push_back(options);
    JSONRPCRequest requestSpent(request.context);
    requestSpent.params = paramsFund;
    requestSpent.URI = request.URI;
    return listunspent(requestSpent);  
}
namespace
{

/**
 * Helper class that keeps track of reserved keys that are used for mining
 * coinbases.  We also keep track of the block hash(es) that have been
 * constructed based on the key, so that we can mark it as keep and get a
 * fresh one when one of those blocks is submitted.
 */
class ReservedKeysForMining
{

private:

  /**
   * The per-wallet data that we store.
   */
  struct PerWallet
  {

    /**
     * The current coinbase script.  This has been taken out of the wallet
     * already (and marked as "keep"), but is reused until a block actually
     * using it is submitted successfully.
     */
    CScript coinbaseScript;

    /** All block hashes (in hex) that are based on the current script.  */
    std::set<std::string> blockHashes;

    explicit PerWallet (const CScript& scr)
      : coinbaseScript(scr)
    {}

    PerWallet (PerWallet&&) = default;

  };

  /**
   * Data for each wallet that we have.  This is keyed by CWallet::GetName,
   * which is not perfect; but it will likely work in most cases, and even
   * when two different wallets are loaded with the same name (after each
   * other), the worst that can happen is that we mine to an address from
   * the other wallet.
   */
  std::map<std::string, PerWallet> data;

  /** Lock for this instance.  */
  mutable RecursiveMutex cs;

public:

  ReservedKeysForMining () = default;

  /**
   * Retrieves the key to use for mining at the moment.
   */
  CScript
  GetCoinbaseScript (CWallet* pwallet)
  {
    LOCK2 (cs, pwallet->cs_wallet);

    const auto mit = data.find (pwallet->GetName ());
    if (mit != data.end ())
      return mit->second.coinbaseScript;

    ReserveDestination rdest(pwallet, pwallet->m_default_address_type);
    CTxDestination dest;
    if (!rdest.GetReservedDestination (dest, false))
      throw JSONRPCError (RPC_WALLET_KEYPOOL_RAN_OUT,
                          "Error: Keypool ran out,"
                          " please call keypoolrefill first");
    rdest.KeepDestination ();

    const CScript res = GetScriptForDestination (dest);
    data.emplace (pwallet->GetName (), PerWallet (res));
    return res;
  }

  /**
   * Adds the block hash (given as hex string) of a newly constructed block
   * to the set of blocks for the current key.
   */
  void
  AddBlockHash (const CWallet* pwallet, const std::string& hashHex)
  {
    LOCK (cs);

    const auto mit = data.find (pwallet->GetName ());
    assert (mit != data.end ());
    mit->second.blockHashes.insert (hashHex);
  }

  /**
   * Marks a block as submitted, releasing the key for it (if any).
   */
  void
  MarkBlockSubmitted (const CWallet* pwallet, const std::string& hashHex)
  {
    LOCK (cs);

    const auto mit = data.find (pwallet->GetName ());
    if (mit == data.end ())
      return;

    if (mit->second.blockHashes.count (hashHex) > 0)
      data.erase (mit);
  }

};

ReservedKeysForMining g_mining_keys;

} // anonymous namespace

UniValue getauxblock(const JSONRPCRequest& request)
{

    /* RPCHelpMan::Check is not applicable here since we have the
       custom check for exactly zero or two arguments.  */
    if (request.fHelp
          || (request.params.size() != 0 && request.params.size() != 2))
        throw std::runtime_error(
            RPCHelpMan{"getauxblock",
                "\nCreates or submits a merge-mined block.\n"
                "\nWithout arguments, creates a new block and returns information\n"
                "required to merge-mine it.  With arguments, submits a solved\n"
                "auxpow for a previously returned block.\n",
                {
                    {"hash", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED_NAMED_ARG, "Hash of the block to submit"},
                    {"auxpow", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED_NAMED_ARG, "Serialised auxpow found"},
                },
                {
                    RPCResult{"without arguments",
                        RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::STR_HEX, "hash", "hash of the created block"},
                            {RPCResult::Type::NUM, "chainid", "chain ID for this block"},
                            {RPCResult::Type::STR_HEX, "previousblockhash", "hash of the previous block"},
                            {RPCResult::Type::NUM, "coinbasevalue", "value of the block's coinbase"},
                            {RPCResult::Type::STR, "bits", "compressed target of the block"},
                            {RPCResult::Type::NUM, "height", "height of the block"},
                            {RPCResult::Type::STR, "_target", "target in reversed byte order, deprecated"},
                        },
                    },
                    RPCResult{"with arguments",
                        RPCResult::Type::BOOL, "xxxxx", "whether the submitted block was correct"
                    },
                },
                RPCExamples{
                    HelpExampleCli("getauxblock", "")
                    + HelpExampleCli("getauxblock", "\"hash\" \"serialised auxpow\"")
                    + HelpExampleRpc("getauxblock", "")
                },
            }.ToString());
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();
    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
    }
    /* Create a new block */
    if (request.params.size() == 0)
    {
        const CScript coinbaseScript = g_mining_keys.GetCoinbaseScript(pwallet);
        const UniValue res = AuxpowMiner::get().createAuxBlock(coinbaseScript, request.context);
        g_mining_keys.AddBlockHash(pwallet, res["hash"].get_str ());
        return res;
    }

    /* Submit a block instead.  */
    CHECK_NONFATAL(request.params.size() == 2);
    const std::string& hash = request.params[0].get_str();

    const bool fAccepted
        = AuxpowMiner::get().submitAuxBlock(hash, request.params[1].get_str(), request.context);
    if (fAccepted)
        g_mining_keys.MarkBlockSubmitted(pwallet, hash);

    return fAccepted;
}

Span<const CRPCCommand> GetAssetWalletRPCCommands()
{
// clang-format off
static const CRPCCommand commands[] =
{ 
    //  category              name                                actor (function)                argNames
    //  --------------------- ------------------------          -----------------------         ----------

   /* assets using the blockchain, coins/points/service backed tokens*/
    { "syscoinwallet",            "syscoinburntoassetallocation",     &syscoinburntoassetallocation,  {"asset_guid","amount"} }, 
    { "syscoinwallet",            "convertaddresswallet",             &convertaddresswallet,          {"address","label","rescan"} },
    { "syscoinwallet",            "assetallocationburn",              &assetallocationburn,           {"asset_guid","amount","ethereum_destination_address"} }, 
    { "syscoinwallet",            "assetallocationmint",              &assetallocationmint,           {"asset_guid","address","amount","blocknumber","bridge_transfer_id","tx_hex","txmerkleproof_hex","txmerkleproofpath_hex","receipt_hex","receiptmerkleproof","auxfee_test"} },     
    { "syscoinwallet",            "assetnew",                         &assetnew,                      {"funding_amount","symbol","description","contract","precision","total_supply","max_supply","updatecapability_flags","notary_address","auxfee_address","notary_details","auxfee_details"}},
    { "syscoinwallet",            "assetnewtest",                     &assetnewtest,                  {"asset_guid","funding_amount","symbol","description","contract","precision","total_supply","max_supply","updatecapability_flags","notary_address","auxfee_address","notary_details","auxfee_details"}},
    { "syscoinwallet",            "assetupdate",                      &assetupdate,                   {"asset_guid","description","contract","supply","updatecapability_flags","notary_address","auxfee_address","notary_details","auxfee_details"}},
    { "syscoinwallet",            "assettransfer",                    &assettransfer,                 {"asset_guid","address"}},
    { "syscoinwallet",            "assetsend",                        &assetsend,                     {"asset_guid","address","amount"}},
    { "syscoinwallet",            "assetsendmany",                    &assetsendmany,                 {"asset_guid","amounts"}},
    { "syscoinwallet",            "assetallocationsend",              &assetallocationsend,           {"asset_guid","address_receiver","amount","replaceable"}},
    { "syscoinwallet",            "assetallocationsendmany",          &assetallocationsendmany,       {"amounts","replaceable","comment","conf_target","estimate_mode"}},
    { "syscoinwallet",            "listunspentasset",                 &listunspentasset,              {"asset_guid","minconf"}},
    { "syscoinwallet",            "signhash",                         &signhash,                      {"address","hash"}},
    
    /** Auxpow wallet functions */
    { "syscoinwallet",             "getauxblock",                      &getauxblock,                   {"hash","auxpow"} },
};
// clang-format on
    return MakeSpan(commands);
}
