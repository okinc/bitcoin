/*
 * cokmonitor.h
 *
 *  Created on: 2016年6月12日
 *      Author: chenzs
 */

#ifndef COKMONITOR_H
#define COKMONITOR_H

#include <string>
#include <vector>
#include <queue>
#include <boost/unordered_map.hpp>
#include <stdint.h>
#include <functional>
#include <boost/thread.hpp>

#include "uint256.h"
#include "sync.h"
#include "dbwrapper.h"

// -moncache default (MiB)
static const int64_t nDefaultMonCache = 100;
// max. -moncache in (MiB)
static const int64_t nMaxMonCache = sizeof(void*) > 4 ? 4096 : 1024;
// min. -moncache in (MiB)
static const int64_t nMinMonCache = 4;


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


class COKMonitor : public CDBWrapper
{
private:
    COKMonitor(const COKMonitor&);
    void operator=(const COKMonitor&);

public:

    virtual void Start();
    virtual void Stop();

    virtual bool ack(const std::string &requestId);
    virtual void SyncConnectBlock(const CBlock *pblock,
                          CBlockIndex* pindex,
                          const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    virtual void SyncDisconnectBlock(const CBlock *pblock);

protected:
    void PostThread();
    void AckThread();
    void ResendThread();
    void NoResponseCheckThread();

    void NoResponseCheck();

protected:
    void push_post(const std::string &requestId, const std::string &json);
    void push_acked(const std::string &requestId);
    void push_resend(const std::string &requestId);

    bool pull_post(std::string &requestId, const std::string ** const ppjson);
    bool pull_acked(std::string &requestId, const std::string ** const ppjson);
    bool pull_resend(std::string &requestId, const std::string ** const ppjson);

    bool do_post(const std::string &requestId, const std::string * pjson);
    bool do_acked(const std::string &requestId);
    bool do_resend(const std::string &requestId, const std::string * pjson);

protected:
    int64_t retryDelay;
    int64_t httpPool;
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
    std::priority_queue<std::pair<std::string, int64_t>, std::vector<std::pair<std::string, int64_t> >, LessThanByTime> resendQueue;

    boost::unordered_map<std::string, std::string> requestMap;
    boost::unordered_map<std::string, int64_t> postMap;
    boost::thread_group threadGroup;
};

#endif // COKMONITOR_H
