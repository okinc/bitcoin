/*
 * address-monitor.h
 *
 *  Created on: 2014年10月28日
 *      Author: Administrator
 */

#ifndef ADDRESS_MONITOR_H_
#define ADDRESS_MONITOR_H_

#include <string>
#include <vector>
#include <queue>
#include <boost/unordered_map.hpp>
#include <stdint.h>
#include <functional>
#include <boost/thread.hpp>

#include "uint256.h"
#include "sync.h"
#include "leveldbwrapper.h"

#define ADDRMON_RETRY_DELAY	600
#define ADDRMON_HTTP_POOL	10

// -moncache default (MiB)
static const int64_t nDefaultMonCache = 100;
// max. -moncache in (MiB)
static const int64_t nMaxMonCache = sizeof(void*) > 4 ? 4096 : 1024;
// min. -moncache in (MiB)
static const int64_t nMinMonCache = 4;

class CBlock;
class CBlockIndex;
class CTransaction;


#ifndef HASH_PAIR_UINT256_UINT160
#define HASH_PAIR_UINT256_UINT160

namespace boost
{
    template <> struct hash<uint160>
    {
        size_t operator()(const uint160 &hash) const
        {
            size_t h = 0;

            const unsigned char* end = hash.end();
            for (const unsigned char *it = hash.begin(); it != end; ++it) {
                h = 31 * h + (*it);
            }

            return h;
        }
    };

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

struct LessThanByTime
{
	bool operator()(const std::pair<std::string, int64_t>& r1, const std::pair<std::string, int64_t>& r2) const
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


class AddressMonitor : public CLevelDBWrapper
{
public:
	AddressMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
	AddressMonitor(const AddressMonitor&);
    void operator=(const AddressMonitor&);

    void PushSyncTx(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncTxQueue);
    void PushSyncTxConnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncConnectQueue);
    void PushSyncTxDisconnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncDisconnectQueue);

public:
    mutable CCriticalSection cs_address;

    void Start();
    void Stop();
    //添加监听地址
    bool AddAddress(const uint160 &keyId, const std::string &address);
    //删除监听地址
    bool DelAddress(const uint160 &keyId, const std::string &address);
    //地址是否被监听
    bool HasAddress(const uint160 &keyId);
    bool ack(const std::string &requestId);

    void SyncTransaction(const CTransaction &tx, const CBlock *pblock, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);
    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    //RPC:resynctx同步tx使用
    void SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const CTransaction &tx, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());



protected:
    boost::unordered_map<int, uint160> GetMonitoredAddresses(const CTransaction &tx, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());

private:
    void PostThread();
    void AckThread();
    void ResendThread();
    void NoResponseCheckThread();

    void NoResponseCheck();

private:
    int64_t retryDelay;
    int64_t httpPool;
    //加载监听地址
    bool LoadAddresses();
    bool LoadCacheTransactions();

    bool WriteAddress(const uint160 &keyId, const std::string &address);
    bool DeleteAddress(const uint160 &keyId);

    enum
    {
    	SYNC_TX = 1,
    	SYNC_CONNECT = 2,
    	SYNC_DISCONNECT = 3
    };

    bool WriteCacheTx(const int64_t &timestamp, const uint256 &uuid, const int type, const std::string &json);
    bool DeleteCacheTx(const int64_t &timestamp, const uint256 &uuid);

    boost::unordered_map<uint160, std::string> addressMap;//监听地址
    boost::unordered_map<std::pair<uint256, uint160>, std::pair<int, bool> > txMap;

    const uint256 NewRandomUUID() const;
    const std::string NewRequestId() const;
    const std::string NewRequestId(const int64_t &now, const uint256 &uuid) const;
    bool decodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid);
    bool decodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid);

private:

    mutable CCriticalSection cs_post;
    mutable CCriticalSection cs_postMap;
    mutable CCriticalSection cs_acked;
    mutable CCriticalSection cs_resend;
    mutable CCriticalSection cs_map;

    mutable CSemaphore sem_post;
    mutable CSemaphore sem_acked;
    mutable CSemaphore sem_resend;

    std::queue<std::string> postQueue;
    boost::unordered_map<std::string, int64_t> postMap;
    std::queue<std::string> ackedQueue;
    std::priority_queue<std::pair<std::string, int64_t>,
    	std::vector<std::pair<std::string, int64_t> >, LessThanByTime> resendQueue;
    boost::unordered_map<std::string, std::string>
    	requestMap;

    void push_post(const std::string &requestId, const std::string &json);
    void push_acked(const std::string &requestId);
    void push_resend(const std::string &requestId);

    bool pull_post(std::string &requestId, const std::string ** const ppjson);
    bool pull_acked(std::string &requestId, const std::string ** const ppjson);
    bool pull_resend(std::string &requestId, const std::string ** const ppjson);

    bool do_post(const std::string &requestId, const std::string * pjson);
    bool do_acked(const std::string &requestId);
    bool do_resend(const std::string &requestId, const std::string * pjson);

    boost::thread_group threadGroup;
    bool is_stop;
};



#endif /* ADDRESS_MONITOR_H_ */
