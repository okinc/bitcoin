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

 //add by oklink
COKBlockChainMonitor *pOkBlkMonitor = NULL;

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
    g_signals.UpdatedBlockTip.connect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1));
    g_signals.SyncTransaction.connect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2, _3));
    g_signals.UpdatedTransaction.connect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.SetBestChain.connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.Inventory.connect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
    g_signals.Broadcast.connect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.BlockChecked.connect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
    g_signals.ScriptForMining.connect(boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, _1));
    g_signals.BlockFound.connect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, _1));

    ///////////////////////////////////////
     // add by oklink
     g_signals.SyncTransaction.connect(boost::bind(&COKBlockChainMonitor::SyncTransaction, pOkBlkMonitor, _1, _3, _4, _5));
     g_signals.SyncConnectBlock.connect(boost::bind(&COKBlockChainMonitor::SyncConnectBlock, pOkBlkMonitor, _1, _2, _3));
     g_signals.SyncDisconnectBlock.connect(boost::bind(&COKBlockChainMonitor::SyncDisconnectBlock, pOkBlkMonitor, _1));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {
    g_signals.BlockFound.disconnect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, _1));
    g_signals.ScriptForMining.disconnect(boost::bind(&CValidationInterface::GetScriptForMining, pwalletIn, _1));
    g_signals.BlockChecked.disconnect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
    g_signals.Broadcast.disconnect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn, _1));
    g_signals.Inventory.disconnect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
    g_signals.SetBestChain.disconnect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
    g_signals.UpdatedTransaction.disconnect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
    g_signals.SyncTransaction.disconnect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2, _3));
    g_signals.UpdatedBlockTip.disconnect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1));
    ////////////////////////////////////////
     // add by oklink
     g_signals.SyncTransaction.disconnect(boost::bind(&COKBlockChainMonitor::SyncTransaction, pOkBlkMonitor, _1, _3, _4, _5));
     g_signals.SyncConnectBlock.disconnect(boost::bind(&COKBlockChainMonitor::SyncConnectBlock, pOkBlkMonitor, _1, _2, _3));
     g_signals.SyncDisconnectBlock.disconnect(boost::bind(&COKBlockChainMonitor::SyncDisconnectBlock, pOkBlkMonitor, _1));


}

void UnregisterAllValidationInterfaces() {
    g_signals.BlockFound.disconnect_all_slots();
    g_signals.ScriptForMining.disconnect_all_slots();
    g_signals.BlockChecked.disconnect_all_slots();
    g_signals.Broadcast.disconnect_all_slots();
    g_signals.Inventory.disconnect_all_slots();
    g_signals.SetBestChain.disconnect_all_slots();
    g_signals.UpdatedTransaction.disconnect_all_slots();
    g_signals.SyncTransaction.disconnect_all_slots();
    g_signals.UpdatedBlockTip.disconnect_all_slots();
    ////////////////////////////////////////
     //below add by oklink
     g_signals.SyncConnectBlock.disconnect_all_slots();
     g_signals.SyncDisconnectBlock.disconnect_all_slots();
}

void SyncWithWallets(const CTransaction &tx, const CBlockIndex *pindex, const CBlock *pblock, CNode *pfrom, bool fConflicted) {
    g_signals.SyncTransaction(tx, pindex, pblock, pfrom, fConflicted);
}

//add by oklink
void SyncWithBlock(const CBlock& block,  CBlockIndex* pindex,  CNode *pfrom){
    g_signals.SyncConnectBlock(&block, pindex,  pfrom);
}
