/*
 * block-monitor.h
 *
 *  Created on: 2015年3月26日
 *      Author: Administrator
 */

#ifndef BLOCK_MONITOR_H_
#define BLOCK_MONITOR_H_

#include "cokmonitor.h"

#define BLOCKMON_RETRY_DELAY	600
#define BLOCKMON_HTTP_POOL	10



class BlockMonitor : public COKMonitor
{
public:
	BlockMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    virtual ~BlockMonitor();
private:
    BlockMonitor(const BlockMonitor&);
    void operator=(const BlockMonitor&);

    void LoadSyncConnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncConnectQueue);
    void LoadSyncDisconnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncDisconnectQueue);

public:

    void Start();
    void SyncConnectBlock(const CBlock *pblock,
                          CBlockIndex* pindex,
                          const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);

    virtual void PostActionWrappedException(const std::string &requestId, const std::string& body);
    virtual bool AckWrappedExceptioin(const int64_t &timestamp, const uint256 &uuid);

private:
    void PostThread();
    void AckThread();
    void ResendThread();
    void NoResponseCheckThread();

//    void NoResponseCheck();

private:

    bool LoadBlocks();

    enum
    {
    	SYNC_CONNECT = 1,
    	SYNC_DISCONNECT = 2
    };

    bool WriteBlock(const int64_t &timestamp, const uint256 &uuid, const int type, const std::string &json);
    bool DeleteBlock(const int64_t &timestamp, const uint256 &uuid);

};


#endif /* BLOCK_MONITOR_H_ */
