#!/usr/bin/env python3
# Copyright (c) 2019-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import SyscoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class AssetTest(SyscoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.rpc_timeout = 240
        self.extra_args = [['-assetindex=1'],['-assetindex=1']]

    def run_test(self):
        self.nodes[0].generate(200)
        self.basic_asset()
        self.asset_description_too_big()
        self.asset_symbol_size()
        self.asset_maxsupply()
        self.asset_transfer()

    def basic_asset(self):
        asset = self.nodes[0].assetnew('1', 'TST', 'asset description', '0x', 8, 1000, 10000, 255, '', '', {}, {})['asset_guid']
        self.sync_mempools()
        self.nodes[1].generate(3)
        self.sync_blocks()
        assetInfo = self.nodes[0].assetinfo(asset)
        assert_equal(assetInfo['asset_guid'], asset)
        assetInfo = self.nodes[1].assetinfo(asset)
        assert_equal(assetInfo['asset_guid'], asset)

    def asset_description_too_big(self):
        # 373 + 11 byte overhead for pub data descriptor and json = 384 bytes long (512 bytes limit base64 encoded)
        gooddata = "SfsddfsdffsdfdfsdsfdsddsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfddSfsddfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsdsffsdfdfsdsdfdfsdfdfsdfdfsd"
        # 384 bytes long + 1 (base64 encoded should be more than 1024 bytes)
        baddata = gooddata + "a"
        asset = self.nodes[0].assetnew('1', 'TST', gooddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})['asset_guid']
        asset1 = self.nodes[0].assetnew('1', 'TST', gooddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})['asset_guid']
        self.nodes[0].generate(1)
        self.sync_blocks()
        assetInfo = self.nodes[0].assetinfo(asset)
        assert_equal(assetInfo['asset_guid'], asset)
        assetInfo = self.nodes[1].assetinfo(asset)
        assert_equal(assetInfo['asset_guid'], asset)
        assetInfo = self.nodes[0].assetinfo(asset1)
        assert_equal(assetInfo['asset_guid'], asset1)
        assetInfo = self.nodes[1].assetinfo(asset1)
        assert_equal(assetInfo['asset_guid'], asset1)
        # data too big
        assert_raises_rpc_error(-4, 'asset-pubdata-too-big', self.nodes[0].assetnew, '1', 'TST', baddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})
    
    def asset_symbol_size(self):
        gooddata = 'asset description'
        self.nodes[0].assetnew('1', 'T', gooddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'asset-invalid-symbol', self.nodes[0].assetnew, '1', '', gooddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})
        self.nodes[0].assetnew('1', 'ABCDEFGHI', gooddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'asset-invalid-symbol', self.nodes[0].assetnew, '1', 'ABCDEFGHIJ', gooddata, '0x', 8, 1000, 10000, 255, '', '', {}, {})
        self.nodes[0].generate(1)

    def asset_maxsupply(self):
        # 373 bytes long (512 with overhead)
        gooddata = "SfsddfsdffsdfdfsdsfdsddsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsDfdfddSfsddfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsfsdsfdsfsdsfdsfsdsfsdSfsdfdfsdsfSfsdfdfsdsfDsdsdsdsfsfsdsdsffsdfdfsdsdfdfsdfdfsdfdfsd"
        asset = self.nodes[0].assetnew('1', 'TST', gooddata, '0x', 8, 1, 1, 255, '', '', {}, {})['asset_guid']
        self.nodes[0].generate(1)
        # cannot increase supply
        assert_raises_rpc_error(-4, 'asset-invalid-supply', self.nodes[0].assetupdate, asset, '', '', 0.1, 255, '', '', {}, {})
        asset = self.nodes[0].assetnew('1', 'TST', gooddata, '0x', 8, 1, 2, 255, '', '', {}, {})['asset_guid']
        self.nodes[0].generate(1)
        self.nodes[0].assetupdate(asset, '', '', 0.1, 255, '', '', {}, {})
        self.nodes[0].generate(1)
        self.nodes[0].assetupdate(asset, '', '', 0.9, 255, '', '', {}, {})
        self.nodes[0].generate(1)
        # would go over 2 coins supply
        assert_raises_rpc_error(-4, 'asset-invalid-supply', self.nodes[0].assetupdate, asset, '', '', 0.1, 255, '', '', {}, {})
        self.nodes[0].generate(1)
        # balance > max supply
        assert_raises_rpc_error(-4, 'asset-invalid-supply', self.nodes[0].assetnew, '1', 'TST', gooddata, '0x', 8, 2, 1, 255, '', '', {}, {})
        # int64 limits
        # largest decmal amount that we can use, without compression overflow of uint (~1 quintillion)
        # 10^18 - 1
        maxUint = 999999999999999999
        asset = self.nodes[0].assetnew('1', 'TST', gooddata, '0x', 0, maxUint-1, maxUint, 255, '', '', {}, {})['asset_guid']
        self.nodes[0].generate(1)
        assetInfo = self.nodes[0].assetinfo(asset)
        assert_equal(assetInfo['asset_guid'], asset)
        assert_equal(assetInfo['total_supply'], maxUint-1)
        assert_equal(assetInfo['balance'], maxUint-1)
        assert_equal(assetInfo['max_supply'], maxUint)
        self.nodes[0].assetupdate(asset, '', '', 1, 255, '', '', {}, {})
        self.nodes[0].generate(1)
        assetInfo = self.nodes[0].assetinfo(asset)
        assert_equal(assetInfo['asset_guid'], asset)
        assert_equal(assetInfo['total_supply'], maxUint)
        assert_equal(assetInfo['balance'], maxUint)
        assert_equal(assetInfo['max_supply'], maxUint)
        assert_raises_rpc_error(-4, 'asset-amount-outofrange', self.nodes[0].assetupdate, asset, '', '', 1, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'asset-invalid-maxsupply', self.nodes[0].assetnew, '1', 'TST', gooddata, '0x', 0, maxUint, maxUint+1, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'asset-invalid-maxsupply', self.nodes[0].assetnew, '1', 'TST', gooddata, '0x', 0, maxUint+1, maxUint+1, 255, '', '', {}, {})

    def asset_transfer(self):
        useraddress1 = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(useraddress1, 152)
        self.nodes[0].generate(1)
        self.sync_blocks()
        asset0 = self.nodes[0].assetnew('1', 'TST', 'asset description', '0x', 8, 1000, 10000, 255, '', '', {}, {})['asset_guid']
        asset1 = self.nodes[1].assetnew('1', 'TST', 'asset description', '0x', 8, 1000, 10000, 255, '', '', {}, {})['asset_guid']
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[0].assetupdate(asset0, '', '', 0, 255, '', '', {}, {})
        self.nodes[1].assetupdate(asset1, '', '', 0, 255, '', '', {}, {})
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[0].assettransfer(asset0, self.nodes[1].getnewaddress())
        self.nodes[1].assettransfer(asset1, self.nodes[0].getnewaddress())
        assert_raises_rpc_error(-4, 'No inputs found for this asset', self.nodes[0].assetupdate, asset0, '', '', 0, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'No inputs found for this asset', self.nodes[1].assetupdate, asset1, '', '', 0, 255, '', '', {}, {})
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()
        assert_raises_rpc_error(-4, 'No inputs found for this asset', self.nodes[0].assetupdate, asset0, '', '', 0, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'No inputs found for this asset', self.nodes[1].assetupdate, asset1, '', '', 0, 255, '', '', {}, {})
        assert_raises_rpc_error(-4, 'No inputs found for this asset', self.nodes[0].assettransfer, asset0, self.nodes[1].getnewaddress())
        assert_raises_rpc_error(-4, 'No inputs found for this asset', self.nodes[1].assettransfer, asset1, self.nodes[0].getnewaddress())
        self.nodes[0].assetupdate(asset1, '', '', 0, 255, '', '', {}, {})
        self.nodes[1].assetupdate(asset0, '', '', 0, 255, '', '', {}, {})
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[0].assettransfer(asset1, self.nodes[1].getnewaddress())
        self.nodes[1].assettransfer(asset0, self.nodes[0].getnewaddress())
        self.sync_mempools()
        self.nodes[0].generate(1)
        self.sync_blocks()
        self.nodes[0].assetupdate(asset0, '', '', 0, 255, '', '', {}, {})
        self.nodes[1].assetupdate(asset1, '', '', 0, 255, '', '', {}, {})
        self.sync_mempools()
        self.nodes[0].generate(1)

if __name__ == '__main__':
    AssetTest().main()
