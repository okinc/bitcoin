// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATIONINTERFACE_H
#define BITCOIN_VALIDATIONINTERFACE_H

#include <boost/signals2/signal.hpp>
#include <boost/unordered_map.hpp>
#include "block-monitor/okblockchain-monitor.h"

#include <algorithm>

class CBlock;
struct CBlockLocator;
class CTransaction;
class CValidationInterface;
class CValidationState;
class uint256;

// These functions dispatch to one or all registered wallets

/** Register a wallet to receive updates from core */
void RegisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = NULL);

/** add by oklink*/
void SyncWithBlock(const CBlock& block,  CBlockIndex* pindex);

class CValidationInterface {
protected:
    virtual void SyncTransaction(const CTransaction &tx, const CBlock *pblock,const boost::unordered_map<uint160, std::string> &addresses) {}
    virtual void EraseFromWallet(const uint256 &hash) {}
    virtual void SetBestChain(const CBlockLocator &locator) {}
    virtual void UpdatedTransaction(const uint256 &hash) {}
    virtual void Inventory(const uint256 &hash) {}
    virtual void ResendWalletTransactions(int64_t nBestBlockTime) {}
    virtual void BlockChecked(const CBlock&, const CValidationState&) {}

    virtual void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>()) {};
    virtual void SyncDisconnectBlock(const CBlock *pblock) {};

    friend void ::RegisterValidationInterface(CValidationInterface*);
    friend void ::UnregisterValidationInterface(CValidationInterface*);
    friend void ::UnregisterAllValidationInterfaces();
};

struct CMainSignals {
    /** Notifies listeners of updated transaction data (transaction, and optionally the block it is found in. */
    boost::signals2::signal<void (const CTransaction &, const CBlock *, const boost::unordered_map<uint160, std::string> &)> SyncTransaction;
    /** Notifies listeners of an erased transaction (currently disabled, requires transaction replacement). */
    boost::signals2::signal<void (const uint256 &)> EraseTransaction;
    /** Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible). */
    boost::signals2::signal<void (const uint256 &)> UpdatedTransaction;
    /** Notifies listeners of a new active block chain. */
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    /** Notifies listeners about an inventory item being seen on the network. */
    boost::signals2::signal<void (const uint256 &)> Inventory;
    /** Tells listeners to broadcast their data. */
    boost::signals2::signal<void (int64_t nBestBlockTime)> Broadcast;
    /** Notifies listeners of a block validation result */
    boost::signals2::signal<void (const CBlock&, const CValidationState&)> BlockChecked;

    //add by oklink
    // Notifies listeners of updated transaction data (passing hash, transaction, and optionally the block it is found in.
    boost::signals2::signal<void (const CBlock *, CBlockIndex*, const boost::unordered_map<uint160, std::string> &)> SyncConnectBlock;
    boost::signals2::signal<void (const CBlock *)> SyncDisconnectBlock;
};

CMainSignals& GetMainSignals();
/** add by oklink */
extern COKBlockChainMonitor *pOkBlkMonitor;
//extern BlockMonitor *pblockMonitor;


#endif // BITCOIN_VALIDATIONINTERFACE_H
