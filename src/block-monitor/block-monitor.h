/*
 * block-monitor.h
 *
 *  Created on: 2015年3月26日
 *      Author: Administrator
 */

#ifndef BLOCK_MONITOR_H_
#define BLOCK_MONITOR_H_

#include <string>
#include <vector>
#include <queue>
#include <boost/unordered_map.hpp>
#include <stdint.h>
#include <functional>
#include <boost/thread.hpp>

#include "uint256.h"
#include "timedata.h"
#include "sync.h"
#include "leveldbwrapper.h"

#define BLOCKMON_RETRY_DELAY	600
#define BLOCKMON_HTTP_POOL	10

// -moncache default (MiB)
static const int64_t nDefaultBlockCache = 100;
// max. -moncache in (MiB)
static const int64_t nMaxBlockCache = sizeof(void*) > 4 ? 4096 : 1024;
// min. -moncache in (MiB)
static const int64_t nMinBlockCache = 4;

class CBlock;
class CBlockIndex;
class CTransaction;


#ifndef HASH_PAIR_UINT256_UINT160
#define HASH_PAIR_UINT256_UINT160

namespace boost
{
	template <> struct hash<std::pair<uint256, uint160> >
	{
		size_t operator()(const std::pair<uint256, uint160> &txIdAndKeyId) const
		{
			size_t h = 0;
			const uint256 &hash1 = txIdAndKeyId.first;
			const uint160 &hash2 = txIdAndKeyId.second;

			const unsigned char* end1 = hash1.end();
			for (const unsigned char *it = hash1.begin(); it != end1; ++it) {
				h = 31 * h + (*it);
			}

			const unsigned char* end2 = hash2.end();
			for (const unsigned char *it = hash2.begin(); it != end2; ++it) {
				h = 31 * h + (*it);
			}

			return h;
		}
	};
}

#endif /* HASH_PAIR_UINT256_UINT160 */

#ifndef LESS_THAN_BY_TIME
#define LESS_THAN_BY_TIME

//用于resendQueue优先级队列Compare
struct LessThanByTime
{
	inline bool operator()(const std::pair<std::string, int64_t>& r1, const std::pair<std::string, int64_t>& r2) const
	{
	  if(r1.second < r2.second)
	  {
		  return  true;
	  }
	  else if(r1.second > r2.second)
	  {
		  return false;
	  }
	  else
	  {
		  return r1.first < r2.first;
	  }
	}
};

#endif /* LESS_THAN_BY_TIME */


class BlockMonitor : public CLevelDBWrapper
{
private:
    int64_t retryDelay;
    int64_t httpPool;

    enum
    {
        SYNC_CONNECT = 1,
        SYNC_DISCONNECT = 2
    };

public:
	BlockMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
	BlockMonitor(const BlockMonitor&);
    void operator=(const BlockMonitor&);

public:

    void Start();
    void Stop();

    bool ack(const std::string &requestId);//异步通知requestID请求成功完成
    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);



private:
    void PostThread();
    void AckThread();
    void ResendThread();
    void NoResponseCheckThread();

    void NoResponseCheck();

    bool LoadCacheBlocks();
    void PushCacheSyncConnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncConnectQueue);
    void PushCacheSyncDisconnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncDisconnectQueue);

    bool WriteCacheBlock(const int64_t &timestamp, const uint256 &uuid, const int type, const std::string &json);
    bool DeleteCacheBlock(const int64_t &timestamp, const uint256 &uuid);

    const uint256 NewRandomUUID() const;
    const std::string NewRequestId() const;
    const std::string NewRequestId(const int64_t &now, const uint256 &uuid) const;
    bool decodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid);
    bool decodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid);

private:
    boost::thread_group threadGroup;
    bool is_stop;

    mutable CCriticalSection cs_post;
    mutable CCriticalSection cs_postMap;
    mutable CCriticalSection cs_acked;
    mutable CCriticalSection cs_resend;
    mutable CCriticalSection cs_map;

    mutable CSemaphore sem_post;
    mutable CSemaphore sem_acked;
    mutable CSemaphore sem_resend;

    std::queue<std::string> postQueue;
    std::queue<std::string> ackedQueue;
    std::priority_queue<std::pair<std::string, int64_t>,
        std::vector<std::pair<std::string, int64_t> >, LessThanByTime> resendQueue;
    boost::unordered_map<std::string, int64_t> postMap; //<requestID, post_time>
    boost::unordered_map<std::string, std::string> requestMap;  //<requestID,jsonContent>


    void push_post(const std::string &requestId, const std::string &json);
    void push_acked(const std::string &requestId);
    void push_resend(const std::string &requestId);

    bool pull_post(std::string &requestId, const std::string ** const ppjson);
    bool pull_acked(std::string &requestId, const std::string ** const ppjson);
    bool pull_resend(std::string &requestId, const std::string ** const ppjson);

    bool do_post(const std::string &requestId, const std::string * pjson);
    bool do_acked(const std::string &requestId);
    bool do_resend(const std::string &requestId, const std::string * pjson);

};


#endif /* BLOCK_MONITOR_H_ */
