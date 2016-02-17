/*
 * transaction-monitor.h
 *
 *  Created on: 2016年1月15日
 *      Author: chenzs
 */


#ifndef OKBLOCKCHAIN_MONITOR_H
#define OKBLOCKCHAIN_MONITOR_H

#include <string>
#include <vector>
#include <queue>
#include <boost/unordered_map.hpp>
#include <stdint.h>
#include <functional>
#include <boost/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio/io_service.hpp>

#include "json/json_spirit_value.h"
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include "uint256.h"
#include "timedata.h"
#include "sync.h"
#include "leveldbwrapper.h"
#include "net.h"
#include "mysql_wrapper/okcoin_log.h"

// -moncache default (MiB)
static const int64_t nDefaultEventCache = 20;
// max. -moncache in (MiB)
static const int64_t nMaxEventCache = sizeof(void*) > 2 ? 256 : 128;
// min. -moncache in (MiB)
static const int64_t nMinEventCache = 4;


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



class COKBlockChainMonitor : public CLevelDBWrapper
{
protected:
    int64_t retryDelay;
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

    enum
    {
        SYNC_CONNECT = 1,
        SYNC_DISCONNECT = 2
    };

public:
    COKBlockChainMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
    COKBlockChainMonitor(const COKBlockChainMonitor&);
    void operator=(const COKBlockChainMonitor&);

public:
    void Start();
    void Stop();

    void SyncTransaction(const CTransaction &tx, const CBlock *pblock, const CNode *pfrom = NULL, bool fConflicted = false);
    void SyncDisconnectBlock(const CBlock *pblock);
    void SyncConnectBlock(const CBlock *pblock, const CBlockIndex* pindex, const CNode *pfrom = NULL);

protected:
    void SendThread();
    void AckThread();
    void ResendThread();
//    void NoResponseCheckThread();



    void PushCacheLogEvents(std::queue<std::pair<std::pair<int64_t, uint256>, COKLogEvent > > &cachedEventQueue);
    void CallOKLogEvent(const std::string &requestId, const COKLogEvent& logEvent);

    const uint256 NewRandomUUID() const;
    const std::string NewRequestId() const;
    const std::string NewRequestId(const int64_t &now, const uint256 &uuid) const;
    bool decodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid);
    bool decodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid);

protected:
     void BuildEvent(const int &action, const CTransaction& tx, const CNode *pfrom = NULL);
     void BuildEvent(const int &action, const CBlock *pblock, const CNode *pfrom = NULL);

     bool LoadCacheEvents();
     bool WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid,  const COKLogEvent& logEvent);
     bool DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid);

     void push_send(const std::string &requestId, const COKLogEvent& logEvent);
     void push_acked(const std::string &requestId);
     void push_resend(const std::string &requestId);

     bool pull_send(std::string &requestId,  const COKLogEvent ** const ppjson);
     bool pull_acked(std::string &requestId, const COKLogEvent ** const ppjson);
     bool pull_resend(std::string &requestId, const COKLogEvent ** const ppjson);

     bool do_send(const std::string &requestId, const COKLogEvent& logEvent);
     bool do_acked(const std::string &requestId);
     bool do_resend(const std::string &requestId, const COKLogEvent& logEvent);

};

#endif //  OKBLOCKCHAIN_MONITOR_H
