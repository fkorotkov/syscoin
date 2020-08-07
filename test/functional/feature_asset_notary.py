#!/usr/bin/env python3
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import SyscoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class AssetNotaryTest(SyscoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 5
        self.rpc_timeout = 240
        self.extra_args = [['-assetindex=1'],['-assetindex=1'],['-assetindex=1'],['-assetindex=1'],['-assetindex=1']]

    def run_test(self):
        self.nodes[0].generate(200)
        self.basic_notary()
        self.complex_notary()

    def basic_notary(self):
        self.basic_asset()
        self.nodes[0].assetsend(self.asset, self.nodes[0].getnewaddress(), 2)
        self.nodes[0].generate(1)
        # will give back hex because notarization doesn't happen in assetallocationsend
        hextx = self.nodes[0].assetallocationsend(self.asset, self.nodes[0].getnewaddress(), 0.4)['hex']
        notarysig = self.nodes[0].signhash(self.notary_address, self.nodes[0].getnotarysighash(hextx, self.asset))
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx, self.asset, notarysig)['hex']
        tx_resigned = self.nodes[0].signrawtransactionwithwallet(hextx_notarized)['hex']
        assert_equal(len(hextx), len(hextx_notarized))
        assert(hextx != hextx_notarized)
        assert(tx_resigned != hextx_notarized)
         # cannot send without notarization
        assert_raises_rpc_error(-26, 'assetallocation-notary-sig', self.nodes[0].sendrawtransaction, hextx)
        assert_raises_rpc_error(-26, 'non-mandatory-script-verify-flag', self.nodes[0].sendrawtransaction, hextx_notarized)
        self.nodes[0].sendrawtransaction(tx_resigned)
        self.nodes[0].generate(1)

    def basic_asset(self):
        self.notary_address = self.nodes[0].getnewaddress()
        notary = {'e': 'https://jsonplaceholder.typicode.com/posts/', 'it': True, 'rx': True}
        self.asset = self.nodes[0].assetnew('1', 'TST', 'asset description', '0x', 8, 1000, 10000, 31, self.notary_address, notary, {})['asset_guid']
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()
        assetInfo = self.nodes[0].assetinfo(self.asset)
        assert_equal(assetInfo['asset_guid'], self.asset)
        assetInfo = self.nodes[1].assetinfo(self.asset)
        assert_equal(assetInfo['asset_guid'], self.asset)

   def complex_notary(self):
        notary = {'e': 'https://jsonplaceholder.typicode.com/posts/', 'it': True, 'rx': True}
        self.notary_address0 = self.nodes[0].getnewaddress()
        self.notary_address1 = self.nodes[1].getnewaddress()
        self.notary_address2 = self.nodes[2].getnewaddress()
        self.notary_address3 = self.nodes[3].getnewaddress()
        self.notary_address4 = self.nodes[4].getnewaddress()
        self.asset0 = self.nodes[0].assetnew('1', 'TST0', 'asset description', '0x', 8, 1000, 10000, 31, self.notary_address0, notary, {})['asset_guid']
        self.asset1 = self.nodes[0].assetnew('1', 'TST1', 'asset description', '0x', 8, 1000, 10000, 31, self.notary_address1, notary, {})['asset_guid']
        self.asset2 = self.nodes[0].assetnew('1', 'TST2', 'asset description', '0x', 8, 1000, 10000, 31, self.notary_address2, notary, {})['asset_guid']
        self.asset3 = self.nodes[0].assetnew('1', 'TST3', 'asset description', '0x', 8, 1000, 10000, 31, self.notary_address3, notary, {})['asset_guid']
        self.asset4 = self.nodes[0].assetnew('1', 'TST4', 'asset description', '0x', 8, 1000, 10000, 31, self.notary_address4, notary, {})['asset_guid']
        self.asset5 = self.nodes[0].assetnew('1', 'TST4', 'asset description', '0x', 8, 1000, 10000, 31, '', {}, {})['asset_guid']
        self.nodes[0].generate(1)
        self.nodes[0].assetsend(self.asset0, self.nodes[0].getnewaddress(), 1)
        self.nodes[0].assetsend(self.asset1, self.nodes[0].getnewaddress(), 2)
        self.nodes[0].assetsend(self.asset2, self.nodes[0].getnewaddress(), 3)
        self.nodes[0].assetsend(self.asset3, self.nodes[0].getnewaddress(), 4)
        self.nodes[0].assetsend(self.asset4, self.nodes[0].getnewaddress(), 5)
        self.nodes[0].assetsend(self.asset5, self.nodes[0].getnewaddress(), 6)
        self.nodes[0].generate(1)
        sendobj = [{"asset_guid":self.asset0,"address":self.nodes[0].getnewaddress(),"amount":0.5},{"asset_guid":self.asset0,"address":self.nodes[0].getnewaddress(),"amount":0.5},{"asset_guid":self.asset1,"address":self.nodes[0].getnewaddress(),"amount":2},{"asset_guid":self.asset2,"address":self.nodes[0].getnewaddress(),"amount":2.5},{"asset_guid":self.asset3,"address":self.nodes[0].getnewaddress(),"amount":3},{"asset_guid":self.asset4,"address":self.nodes[0].getnewaddress(),"amount":3.5},{"asset_guid":self.asset5,"address":self.nodes[0].getnewaddress(),"amount":5}]
        hextx = self.nodes[0].assetallocationsendmany(sendobj)['hex']
        notarysig0 = self.nodes[0].signhash(self.notary_address0, self.nodes[0].getnotarysighash(hextx, self.asset0))
        notarysig1 = self.nodes[1].signhash(self.notary_address1, self.nodes[0].getnotarysighash(hextx, self.asset1))
        notarysig2 = self.nodes[2].signhash(self.notary_address2, self.nodes[0].getnotarysighash(hextx, self.asset2))
        notarysig3 = self.nodes[3].signhash(self.notary_address3, self.nodes[0].getnotarysighash(hextx, self.asset3))
        notarysig4 = self.nodes[4].signhash(self.notary_address4, self.nodes[0].getnotarysighash(hextx, self.asset4))
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx, self.asset0, notarysig0)['hex']
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx_notarized, self.asset1, notarysig1)['hex']
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx_notarized, self.asset2, notarysig2)['hex']
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx_notarized, self.asset3, notarysig3)['hex']
        # try without final signature
        tx_resigned = self.nodes[0].signrawtransactionwithwallet(hextx_notarized)['hex']
        assert_raises_rpc_error(-26, 'assetallocation-notary-sig', self.nodes[0].sendrawtransaction, tx_resigned)
        # try with wrong signature
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx_notarized, self.asset4, notarysig3)['hex']
        assert_raises_rpc_error(-26, 'assetallocation-notary-sig', self.nodes[0].sendrawtransaction, tx_resigned)
        hextx_notarized = self.nodes[0].assettransactionnotarize(hextx_notarized, self.asset4, notarysig4)['hex']
        tx_resigned = self.nodes[0].signrawtransactionwithwallet(hextx_notarized)['hex']
        assert_equal(len(hextx), len(hextx_notarized))
        assert(hextx != hextx_notarized)
        assert(tx_resigned != hextx_notarized)

        self.nodes[0].sendrawtransaction(tx_resigned)
        self.nodes[0].generate(1)
if __name__ == '__main__':
    AssetNotaryTest().main()
