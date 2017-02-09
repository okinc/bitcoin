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


COKMonitor::COKMonitor()
{

}

void COKMonitor::push_post(const std::string &requestId, const std::string &json)
{
    LOCK2(cs_map, cs_post);

    requestMap.insert(make_pair(requestId, json));
    postQueue.push(requestId);

    sem_post.post();
}

void COKMonitor::push_acked(const std::string &requestId)
{
    LOCK(cs_acked);

    ackedQueue.push(requestId);

    sem_acked.post();
}

void COKMonitor::push_resend(const std::string &requestId)
{
    LOCK(cs_resend);

    resendQueue.push(make_pair(requestId, GetAdjustedTime() + retryDelay));

    sem_resend.post();
}


bool COKMonitor::pull_post(std::string &requestId, const std::string ** const ppjson)
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

bool COKMonitor::pull_acked(std::string &requestId, const std::string ** const ppjson)
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

bool COKMonitor::pull_resend(std::string &requestId, const std::string ** const ppjson)
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


bool COKMonitor::do_post(const std::string &requestId, const std::string * pjson)
{
    LogAddrmon("do_post -> requestId: "+requestId+", json: "+*pjson+"\n");
    ioService.post(boost::bind(CallRPCWrappedException, this, requestId, *pjson));
    return true;
}

bool COKMonitor::do_acked(const std::string &requestId)
{
    int64_t timestamp;
    uint256 uuid;
    if(!decodeRequestIdWitPrefix(requestId, timestamp, uuid))
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

    return DeleteTx(timestamp, uuid);
}

bool COKMonitor::do_resend(const std::string &requestId, const std::string * pjson)
{
    LogAddrmon("do_resend -> requestId: "+requestId+", json: "+*pjson+"\n");
    ioService.post(boost::bind(CallRPCWrappedException, this, requestId, *pjson));
    return true;
}
