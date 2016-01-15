#include "eventmonitor.h"

#include "serialize.h"
#include "util.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"
#include "rpcserver.h"

#include <sstream>

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

//#include "mysql_wrapper/okcoin_log.h"


#define MONITOR_RETRY_DELAY	60
#define MONITOR_WORK_POOL	5


using namespace std;
using namespace boost;
using namespace json_spirit;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;

static boost::asio::io_service ioService;
static boost::asio::io_service::work threadPool(ioService);


static void io_service_run(void)
{
    ioService.run();
}

/////////////////////////////

CEventMonitor::CEventMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    CLevelDBWrapper(GetDataDir() / "blocks" / "blockmon", nCacheSize, fMemory, fWipe),
    retryDelay(MONITOR_RETRY_DELAY), workPool(MONITOR_WORK_POOL), is_stop(false), sem_send(0), sem_acked(0),  sem_resend(0)
{
    retryDelay = GetArg("-blockmon_retry_delay", MONITOR_RETRY_DELAY);
    workPool = GetArg("-blockmon_work_pool", MONITOR_WORK_POOL);
}

void CEventMonitor::Start()
{
    if(!LoadCacheEvents())
    {
        throw runtime_error("CEventMonitor LoadEvent fail!");
    }

    for(int i = 0; i < workPool; i++)
    {
        threadGroup.create_thread(boost::bind(&io_service_run));
    }

    threadGroup.create_thread(boost::bind(&CEventMonitor::SendThread, this));
    threadGroup.create_thread(boost::bind(&CEventMonitor::AckThread, this));
    threadGroup.create_thread(boost::bind(&CEventMonitor::ResendThread, this));

     //非网络无需做无响应检测
   // threadGroup.create_thread(boost::bind(&CEventMonitor::NoResponseCheckThread, this));
}

void CEventMonitor::Stop()
{
    is_stop = true;
    sem_send.post();
    sem_acked.post();
    sem_resend.post();

    ioService.stop();
    threadGroup.interrupt_all();
    threadGroup.join_all();
}

bool CEventMonitor::ack(const string &requestId)
{
    push_acked(requestId);
    return true;
}

const uint256 CEventMonitor::NewRandomUUID() const
{
    uint256 uuid;
    RandAddSeedPerfmon();

    RAND_bytes(uuid.begin(), uuid.end() - uuid.begin());
    return uuid;
}

const std::string CEventMonitor::NewRequestId(const int64_t &now, const uint256 &uuid) const
{
    CDataStream ssId(SER_DISK, CLIENT_VERSION);
    ssId << now;
    ssId << uuid;

    return EncodeBase58((const unsigned char*)&ssId[0], (const unsigned char*)&ssId[0] + (int)ssId.size());
}

bool CEventMonitor::decodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid)
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

bool CEventMonitor::decodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid)
{
    if(requestIdWithPrefix.substr(0, 6) == "event-")
    {
        return decodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(6), now, uuid);
    }
    else
    {
        return false;
    }
}

const std::string CEventMonitor::NewRequestId() const
{
    const int64_t now = GetAdjustedTime();
    const uint256 uuid = NewRandomUUID();

    return NewRequestId(now, uuid);
}

void CEventMonitor::push_send(const std::string &requestId, const COKLogEvent& logEvent)
{
    LOCK2(cs_map, cs_send);

    requestMap.insert(make_pair(requestId, logEvent));
    sendQueue.push(requestId);

    sem_send.post();
}

void CEventMonitor::push_acked(const std::string &requestId)
{
    LOCK(cs_acked);

    ackedQueue.push(requestId);

    sem_acked.post();
}

void CEventMonitor::push_resend(const std::string &requestId)
{
    LOCK(cs_resend);

    resendQueue.push(make_pair(requestId, GetAdjustedTime() + retryDelay));

    sem_resend.post();
}

bool CEventMonitor::pull_send(std::string &requestId, const COKLogEvent ** const logEvent)
{
    sem_send.wait();
    if(is_stop)
    {
        return false;
    }

    {
        LOCK2(cs_map, cs_send);

        requestId = sendQueue.front();
        sendQueue.pop();

        boost::unordered_map<string, COKLogEvent>::const_iterator it = requestMap.find(requestId);
        if(it == requestMap.end())
        {
            throw runtime_error("pull_send can not find request in map: "+requestId);
        }

        *logEvent = &it->second;
    }

    {
        LOCK(cs_sendMap);
        sendMap.insert(make_pair(requestId, GetAdjustedTime() + retryDelay));
    }

    return true;
}

bool CEventMonitor::pull_acked(std::string &requestId, const COKLogEvent ** const logEvent)
{
    sem_acked.wait();
    if(is_stop)
    {
        return false;
    }

    LOCK2(cs_map, cs_acked);

    requestId = ackedQueue.front();
    ackedQueue.pop();

    boost::unordered_map<string, COKLogEvent>::const_iterator it = requestMap.find(requestId);
    if(it == requestMap.end())
    {
        LogBlock("pull_acked can not find request in map: "+requestId+"\n");
        return false;
    }

    *logEvent = &it->second;

    return true;
}

bool CEventMonitor::pull_resend(std::string &requestId, const COKLogEvent ** const logEvent)
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

        boost::unordered_map<string, COKLogEvent>::const_iterator it = requestMap.find(requestId);
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

        *logEvent = &it->second;

        resendQueue.push(make_pair(requestId, now + retryDelay));//继续放入重发队列，并延迟retryDelay

        sem_resend.post();

        return true;
    }

    return false;
}



static void CallOKLogEvent(CEventMonitor* self, const std::string &requestId, const COKLogEvent& logEvent){
    if(logEvent.IsNull())
        return;

    int ret = OKCoin_Log_Event(logEvent);
    if(ret > 0){
        self->ack(requestId);
    }
}

void CEventMonitor::SendThread()
{
    RenameThread("bitcoin-block-monitor-send");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

    while(!is_stop)
    {
        string requestId;
        const COKLogEvent * logEvent;

        if(!pull_send(requestId, &logEvent))
        {
            MilliSleep(1000);
            continue;
        }

        try
        {
            do_send(requestId, *logEvent);
        }
        catch(std::exception &e)
        {
            LogException(&e, string("CEventMonitor::SendThread() -> "+string(e.what())).c_str());
            LogBlock("CEventMonitor::SendThread() -> "+string(e.what())+"\n");
        }
        catch(...)
        {
            LogBlock("CEventMonitor::SendThread() -> unknow exception\n");
        }
    }
}

void CEventMonitor::AckThread()
{
    RenameThread("bitcoin-block-monitor-ack");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

    while(!is_stop)
    {
        string requestId;
        const COKLogEvent * logEvent;

        if(!pull_acked(requestId, &logEvent))
        {
            MilliSleep(1000);
            continue;
        }

        try
        {
            if(do_acked(requestId))
                LogBlock("do_acked success -> requestId: "+requestId+", event: "+logEvent->ToString()+"\n");
            else
                LogBlock("do_acked fail -> requestId: "+requestId+", event: "+logEvent->ToString()+"\n");
        }
        catch(std::exception &e)
        {
            LogException(&e, string("CEventMonitor::AckThread() -> "+string(e.what())).c_str());
            LogBlock("CEventMonitor::AckThread() -> "+string(e.what())+"\n");
        }
        catch(...)
        {
            LogBlock("CEventMonitor::AckThread() -> unknow exception\n");
        }
    }
}

void CEventMonitor::ResendThread()
{
    RenameThread("bitcoin-block-monitor-resend");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

    while(!is_stop)
    {
        string requestId;
        const COKLogEvent * logEvent;

        if(!pull_resend(requestId, &logEvent))
        {
            MilliSleep(1000);
            continue;
        }

        try
        {
            do_resend(requestId, *logEvent);
        }
        catch(std::exception &e)
        {
            LogException(&e, string("CEventMonitor::ResendThread() -> "+string(e.what())).c_str());
            LogBlock("CEventMonitor::ResendThread() -> "+string(e.what())+"\n");
        }
        catch(...)
        {
            LogBlock("CEventMonitor::ResendThread() -> unknow exception\n");
        }
    }
}

//检测httppost无响应（ack）requestId,并放入重发队列
void CEventMonitor::NoResponseCheckThread()
{
    RenameThread("bitcoin-block-monitor-NoResponseCheck");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

    while(true)
    {
        try
        {
            NoResponseCheck();
        }
        catch(std::exception &e)
        {
            LogException(&e, string("CEventMonitor::NoResponseCheckThread() -> "+string(e.what())).c_str());
            LogBlock("CEventMonitor::NoResponseCheckThread() -> "+string(e.what())+"\n");
        }
        catch(...)
        {
            LogBlock("CEventMonitor::NoResponseCheckThread() -> unknow exception\n");
        }

        MilliSleep(retryDelay * 1000);
    }
}

bool CEventMonitor::do_send(const std::string &requestId, const COKLogEvent& logEvent)
{
    LogBlock("do_send -> requestId: "+requestId+", event: "+logEvent.ToString()+"\n");
    ioService.post(boost::bind(CallOKLogEvent, this, requestId, logEvent));
    return true;
}

bool CEventMonitor::do_acked(const std::string &requestId)
{
    int64_t timestamp;
    uint256 uuid;
    if(!decodeRequestIdWitPrefix(requestId, timestamp, uuid))
    {
        return false;
    }

    {
        LOCK(cs_sendMap);
        sendMap.erase(requestId);
    }

    {
        LOCK(cs_map);
        requestMap.erase(requestId);
    }

    return DeleteCacheEvent(timestamp, uuid);
}

bool CEventMonitor::do_resend(const std::string &requestId, const COKLogEvent& logEvent)
{
    LogBlock("do_resend -> requestId: "+requestId+", event: "+logEvent.ToString()+"\n");
    ioService.post(boost::bind(CallOKLogEvent, this, requestId, logEvent));
    return true;
}

void CEventMonitor::NoResponseCheck()
{
    vector<string> timeoutRequestIds;

    {
        LOCK(cs_sendMap);
        int64_t now = GetAdjustedTime();

        for(boost::unordered_map<string, int64_t>::const_iterator it = sendMap.begin(); it != sendMap.end(); ++it)
        {
            if(it->second < now)
            {
                timeoutRequestIds.push_back(it->first);
            }
        }

        BOOST_FOREACH(const string &requestId, timeoutRequestIds)
        {
            sendMap.erase(requestId);
        }
    }

    {
        LOCK(cs_resend);
        int64_t now = GetAdjustedTime();

        BOOST_FOREACH(const string &requestId, timeoutRequestIds)
        {
            resendQueue.push(make_pair(requestId, now));    //加入重发队列
            sem_resend.post();
        }
    }
}

void CEventMonitor::PushCacheLogEvents(std::queue<std::pair<std::pair<int64_t, uint256>, COKLogEvent> > &cachedEventQueue)
{
    RenameThread("bitcoin-event-monitor-LoadCached");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

    while(!cachedEventQueue.empty())
    {
        pair<pair<int64_t, uint256>,  COKLogEvent> request = cachedEventQueue.front();
        cachedEventQueue.pop();
        const COKLogEvent &logEvent = request.second;
        const int64_t &now = request.first.first;
        const uint256 &uuid = request.first.second;
        const string requestId = "event-" + NewRequestId(now, uuid);

        push_send(requestId, logEvent);
    }
}
