/*
 * block-monitor.h
 *
 *  Created on: 2015年3月26日
 *      Author: Administrator
 */

#ifndef BLOCK_MONITOR_H_
#define BLOCK_MONITOR_H_


#include "eventmonitor.h"

class BlockMonitor : public CEventMonitor
{

public:
	BlockMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    BlockMonitor(const BlockMonitor&);
    void operator=(const BlockMonitor&);

public:

    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);

protected:

    void BuildEvent(const int &action, const CBlock *pblock);

    bool LoadCacheEvents();
    bool WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid,  const COKLogEvent& logEvent);
    bool DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid);


};


#endif /* BLOCK_MONITOR_H_ */
