#include "cokmonitor.h"

#include "serialize.h"
#include "util.h"
#include "timedata.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"


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

#include "rpc/protocol.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>

#include "univalue.h"

using namespace std;
using namespace boost;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;


static void CallPostActionWrappedException(COKMonitor* self, const std::string &requestId, const string& body)
{

    self->PostActionWrappedException(requestId, body);
}


COKMonitor::COKMonitor(const boost::filesystem::path& path, size_t nCacheSize, bool fMemory, bool fWipe):
    CDBWrapper(path, nCacheSize, fMemory, fWipe),retryDelay(500),httpPool(10),sem_post(0),sem_acked(0),sem_resend(0),is_stop(false)
{

}

COKMonitor::~COKMonitor() {

}

/**
 * @brief COKMonitor::push_post
 * @param requestId
 * @param json
 */
void COKMonitor::Push_post(const std::string &requestId, const std::string &json)
{
    LOCK2(cs_map, cs_post);

    requestMap.insert(make_pair(requestId, json));
    postQueue.push(requestId);

    sem_post.post();
}

void COKMonitor::Push_resend(const std::string &requestId)
{
    LOCK(cs_resend);

    resendQueue.push(make_pair(requestId, GetAdjustedTime() + retryDelay));

    sem_resend.post();
}

void COKMonitor::Push_acked(const std::string &requestId)
{
    LOCK(cs_acked);

    ackedQueue.push(requestId);

    sem_acked.post();
}


/**
 *
 * @brief COKMonitor::Pull_post
 * @param requestId
 * @param ppjson
 * @return
 */
bool COKMonitor::Pull_post(std::string &requestId, const std::string ** const ppjson)
{
    sem_post.wait();
    if(is_stop)
    {
        return false;
    }

    {
        LOCK2(cs_map, cs_post);

        requestId = postQueue.front();
        postQueue.pop();

        unordered_map<string, string>::const_iterator it = requestMap.find(requestId);
        if(it == requestMap.end())
        {
            throw runtime_error("pull_post can not find request in map: "+requestId);
        }

        *ppjson = &it->second;
    }

    {
        LOCK(cs_postMap);
        postMap.insert(make_pair(requestId, GetAdjustedTime() + retryDelay));
    }

    return true;
}

bool COKMonitor::Pull_acked(std::string &requestId, const std::string ** const ppjson)
{
    sem_acked.wait();
    if(is_stop)
    {
        return false;
    }

    LOCK2(cs_map, cs_acked);

    requestId = ackedQueue.front();
    ackedQueue.pop();

    unordered_map<string, std::string>::const_iterator it = requestMap.find(requestId);
    if(it == requestMap.end())
    {
        LogAddrmon("pull_acked can not find request in map: "+requestId+"\n");
        return false;
    }

    *ppjson = &it->second;

    return true;
}

bool COKMonitor::Pull_resend(std::string &requestId, const std::string ** const ppjson)
{
    if(!sem_resend.try_wait())
    {
        return false;
    }
    if(is_stop)
    {
        return false;
    }

    LOCK2(cs_map, cs_resend);

    const int64_t now = GetAdjustedTime();

    for(int i = 0; i < 100; i++)
    {
        pair<std::string, int64_t> requestAndTime = resendQueue.top();
        if(requestAndTime.second > now)
        {
            sem_resend.post();
            return false;
        }

        resendQueue.pop();

        requestId = requestAndTime.first;

        unordered_map<string, string>::const_iterator it = requestMap.find(requestId);
        if(it == requestMap.end())
        {
            if(resendQueue.empty())
            {
                return false;
            }
            else
            {
                continue;
            }
        }

        *ppjson = &it->second;

        resendQueue.push(make_pair(requestId, now + retryDelay));

        sem_resend.post();

        return true;
    }

    return false;
}


bool COKMonitor::Do_post(const std::string &requestId, const std::string * pjson)
{
    ioService.post(boost::bind(CallPostActionWrappedException, this, requestId, *pjson));
    return true;
}

bool COKMonitor::Do_resend(const std::string &requestId, const std::string * pjson)
{
    ioService.post(boost::bind(CallPostActionWrappedException, this, requestId, *pjson));
    return true;
}

bool COKMonitor::Do_acked(const std::string &requestId)
{
    int64_t timestamp;
    uint256 uuid;
    if(!DecodeRequestIdWitPrefix(requestId, timestamp, uuid))
    {
        return false;
    }

    {
        LOCK(cs_postMap);
        postMap.erase(requestId);
    }

    {
        LOCK(cs_map);
        requestMap.erase(requestId);
    }

    return AckWrappedExceptioin(timestamp, uuid);
}


const uint256 COKMonitor::NewRandomUUID() const
{
    uint256 uuid = GetRandHash();
    return uuid;
}

const std::string COKMonitor::NewRequestId() const
{
    const int64_t now = GetAdjustedTime();
    const uint256 uuid = NewRandomUUID();

    return NewRequestId(now, uuid);
}

const std::string COKMonitor::NewRequestId(const int64_t &now, const uint256 &uuid) const
{
    CDataStream ssId(SER_DISK, CLIENT_VERSION);
    ssId << now;
    ssId << uuid;

    return EncodeBase58((const unsigned char*)&ssId[0], (const unsigned char*)&ssId[0] + (int)ssId.size());
}

bool COKMonitor::DecodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid)
{
    std::vector<unsigned char> vch;
    if(!DecodeBase58(requestIdWithoutPrefix, vch))
    {
        return false;
    }

    CDataStream ssId(vch, SER_DISK, CLIENT_VERSION);

    ssId >> now;
    ssId >> uuid;

    return true;
}

bool COKMonitor::DecodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid)
{
    if(requestIdWithPrefix.substr(0, 3) == "tx-")
    {
        return DecodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(3), now, uuid);
    }
    else if(requestIdWithPrefix.substr(0, 5) == "conn-")
    {
        return DecodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(5), now, uuid);
    }
    else if(requestIdWithPrefix.substr(0, 5) == "dis-")
    {
        return DecodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(4), now, uuid);
    }
    else
    {
        return false;
    }
}


void COKMonitor::NoResponseCheck()
{
    vector<string> timeoutRequestIds;

    {
        LOCK(cs_postMap);
        int64_t now = GetAdjustedTime();

        for(unordered_map<string, int64_t>::const_iterator it = postMap.begin(); it != postMap.end(); ++it)
        {
            if(it->second < now)
            {
                timeoutRequestIds.push_back(it->first);
            }
        }

        BOOST_FOREACH(const string &requestId, timeoutRequestIds)
        {
            postMap.erase(requestId);
        }
    }

    {
        LOCK(cs_resend);
        int64_t now = GetAdjustedTime();

        BOOST_FOREACH(const string &requestId, timeoutRequestIds)
        {
            resendQueue.push(make_pair(requestId, now));
            sem_resend.post();
        }
    }
}

 bool COKMonitor::Ack(const std::string &requestId) {
     Push_acked(requestId);
     return true;
 }

void COKMonitor::Stop()
{
    is_stop = true;
    sem_post.post();
    sem_acked.post();
    sem_resend.post();

    ioService.stop();
    threadGroup.interrupt_all();
    threadGroup.join_all();
}

void COKMonitor::Run_io_service()
{
    ioService.run();
}


