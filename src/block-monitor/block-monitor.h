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
    BlockMonitor();
	BlockMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
     ~BlockMonitor();
private:
    //BlockMonitor(const BlockMonitor&);
    //void operator=(const BlockMonitor&);

public:

    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);



protected:

    void BuildEvent(const int &action, const CBlock *pblock);

    bool LoadCacheEvents();
    bool WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid,  const COKLogEvent& logEvent);
    bool DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid);

/*
private:
    boost::thread_group threadGroup;
    bool is_stop;

    mutable CCriticalSection cs_send;
    mutable CCriticalSection cs_sendMap;
    mutable CCriticalSection cs_acked;
    mutable CCriticalSection cs_resend;
    mutable CCriticalSection cs_map;

    mutable CSemaphore sem_send;
    mutable CSemaphore sem_acked;
    mutable CSemaphore sem_resend;

    std::queue<std::string> sendQueue;
    std::queue<std::string> ackedQueue;
    std::priority_queue<std::pair<std::string, int64_t>,
        std::vector<std::pair<std::string, int64_t> >, LessThanByTime> resendQueue;
    boost::unordered_map<std::string, int64_t> sendMap; //<requestID, post_time>
    boost::unordered_map<std::string, COKLogEvent> requestMap;  //<requestID,logEvent>


    void push_send(const std::string &requestId, const COKLogEvent& logEvent);
    void push_acked(const std::string &requestId);
    void push_resend(const std::string &requestId);

    bool pull_send(std::string &requestId,  const COKLogEvent ** const ppjson);
    bool pull_acked(std::string &requestId, const COKLogEvent ** const ppjson);
    bool pull_resend(std::string &requestId, const COKLogEvent ** const ppjson);

    bool do_send(const std::string &requestId, const COKLogEvent& logEvent);
    bool do_acked(const std::string &requestId);
    bool do_resend(const std::string &requestId, const COKLogEvent& logEvent);
*/
};


#endif /* BLOCK_MONITOR_H_ */
