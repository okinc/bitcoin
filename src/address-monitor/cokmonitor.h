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
#include <stdint.h>
#include <functional>

#include <boost/thread.hpp>
#include <boost/unordered_map.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "uint256.h"
#include "sync.h"
#include "dbwrapper.h"

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


class COKMonitor : public CDBWrapper
{
public:
    COKMonitor(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe);
    virtual ~COKMonitor();

public:

    virtual void Start() = 0;
    void Stop();

    void SyncConnectBlock(const CBlock *pblock,
                          CBlockIndex* pindex,
                          const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    void SyncDisconnectBlock(const CBlock *pblock);


    virtual void PostThread() = 0;
    virtual void AckThread() = 0;
    virtual void ResendThread() = 0;
    virtual void NoResponseCheckThread() = 0;

    virtual void PostActionWrappedException(const std::string &requestId, const std::string& body) = 0;
    virtual bool AckWrappedExceptioin(const int64_t &timestamp, const uint256 &uuid) = 0;

     bool Ack(const std::string &requestId);
     void Run_io_service();

protected:

    const uint256 NewRandomUUID() const;
    const std::string NewRequestId() const;
    const std::string NewRequestId(const int64_t &now, const uint256 &uuid) const;
    bool DecodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid);
    bool DecodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid);

    //http请求无响应检测
    void NoResponseCheck();


protected:
    void Push_post(const std::string &requestId, const std::string &json);
    void Push_acked(const std::string &requestId);
    void Push_resend(const std::string &requestId);

    bool Pull_post(std::string &requestId, const std::string ** const ppjson);
    bool Pull_acked(std::string &requestId, const std::string ** const ppjson);
    bool Pull_resend(std::string &requestId, const std::string ** const ppjson);

    bool Do_post(const std::string &requestId, const std::string * pjson);
    bool Do_acked(const std::string &requestId);
    bool Do_resend(const std::string &requestId, const std::string * pjson);

protected:
    int64_t retryDelay;
    int64_t httpPool;


    //临界区
    mutable CCriticalSection cs_post;
    mutable CCriticalSection cs_postMap;
    mutable CCriticalSection cs_acked;
    mutable CCriticalSection cs_resend;
    mutable CCriticalSection cs_map;

    //信号量
    mutable CSemaphore sem_post;
    mutable CSemaphore sem_acked;
    mutable CSemaphore sem_resend;

    std::queue<std::string> postQueue;  //发送队列
    std::queue<std::string> ackedQueue; //完成响应队列
    //重发队列
    std::priority_queue<std::pair<std::string, int64_t>, std::vector<std::pair<std::string, int64_t> >, LessThanByTime> resendQueue;


    boost::unordered_map<std::string, std::string> requestMap;  //请求字典
    boost::unordered_map<std::string, int64_t> postMap; //发送字典

    bool is_stop;
    boost::thread_group threadGroup;
    boost::asio::io_service ioService;
};

#endif // COKMONITOR_H
