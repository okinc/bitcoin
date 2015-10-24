// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"

static CMainSignals g_signals;

CMainSignals& GetMainSignals()
{
    return g_signals;
}

AddressMonitor *paddressMonitor = NULL;
BlockMonitor *pblockMonitor = NULL;

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
    g_signals.SyncTransaction.connect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2, _3));
    g_signals.EraseTransaction.connect(boost::bind(&CValidationInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.UpdatedTransaction.connect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.SetBestChain.connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.Inventory.connect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
    g_signals.Broadcast.connect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.BlockChecked.connect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));

    ////////////////////////////////////////
    //below add by oklink
    g_signals.SyncTransaction.connect(boost::bind(&AddressMonitor::SyncTransaction, paddressMonitor, _1, _2, _3));
    g_signals.SyncConnectBlock.connect(boost::bind(&AddressMonitor::SyncConnectBlock, paddressMonitor, _1, _2, _3));
    g_signals.SyncDisconnectBlock.connect(boost::bind(&AddressMonitor::SyncDisconnectBlock, paddressMonitor, _1));

    g_signals.SyncConnectBlock.connect(boost::bind(&BlockMonitor::SyncConnectBlock, pblockMonitor, _1, _2, _3));
    g_signals.SyncDisconnectBlock.connect(boost::bind(&BlockMonitor::SyncDisconnectBlock, pblockMonitor, _1));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {
    g_signals.BlockChecked.disconnect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
    g_signals.Broadcast.disconnect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.Inventory.disconnect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
    g_signals.SetBestChain.disconnect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.UpdatedTransaction.disconnect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.EraseTransaction.disconnect(boost::bind(&CValidationInterface::EraseFromWallet, pwalletIn, _1));
    g_signals.SyncTransaction.disconnect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2, _3));

    ////////////////////////////////////////
    //below add by oklink
    g_signals.SyncTransaction.disconnect(boost::bind(&AddressMonitor::SyncTransaction, paddressMonitor, _1, _2, _3));
    g_signals.SyncConnectBlock.disconnect(boost::bind(&AddressMonitor::SyncConnectBlock, paddressMonitor, _1, _2, _3));
    g_signals.SyncDisconnectBlock.disconnect(boost::bind(&AddressMonitor::SyncDisconnectBlock, paddressMonitor, _1));

    g_signals.SyncConnectBlock.disconnect(boost::bind(&BlockMonitor::SyncConnectBlock, pblockMonitor, _1, _2, _3));
    g_signals.SyncDisconnectBlock.disconnect(boost::bind(&BlockMonitor::SyncDisconnectBlock, pblockMonitor, _1));
}

void UnregisterAllValidationInterfaces() {
    g_signals.BlockChecked.disconnect_all_slots();
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.Inventory.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.UpdatedTransaction.disconnect_all_slots();
    g_signals.EraseTransaction.disconnect_all_slots();
    g_signals.SyncTransaction.disconnect_all_slots();
    ////////////////////////////////////////
    //below add by oklink
    g_signals.SyncConnectBlock.disconnect_all_slots();
    g_signals.SyncDisconnectBlock.disconnect_all_slots();
}

void SyncWithWallets(const CTransaction &tx, const CBlock *pblock) {
    g_signals.SyncTransaction(tx, pblock, boost::unordered_map<uint160, std::string>());
}

void SyncWithBlock(const CBlock& block,  CBlockIndex* pindex){
    g_signals.SyncConnectBlock(&block, pindex, boost::unordered_map<uint160, std::string>());
}
