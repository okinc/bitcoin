/*
 * transaction-monitor.h
 *
 *  Created on: 2016年1月15日
 *      Author: chenzs
 */


#ifndef TRANSACTIONMONITOR_H
#define TRANSACTIONMONITOR_H

#include "eventmonitor.h"



class TransactionMonitor : public CEventMonitor
{
public:
    TransactionMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
    TransactionMonitor(const TransactionMonitor&);
    void operator=(const TransactionMonitor&);

public:
    void SyncTransaction(const CTransaction &tx, const CBlock *pblock, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);
    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());

//    //RPC:resynctx同步tx使用
//    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const CTransaction &tx, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());

protected:
     void BuildEvent(const int &action, const CTransaction& tx);

     bool LoadCacheEvents();
     bool WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid,  const COKLogEvent& logEvent);
     bool DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid);


};

#endif // TRANSACTIONMONITOR_H
