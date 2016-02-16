#include "okblockchain-monitor.h"

#include "serialize.h"
#include "util.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"

#include "mysql_wrapper/okcoin_log.h"

#define MONITOR_RETRY_DELAY	60

using namespace std;
using namespace boost;
using namespace json_spirit;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;



COKBlockChainMonitor::COKBlockChainMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    CLevelDBWrapper(GetDataDir() / "blocks" / "monitx", nCacheSize, fMemory, fWipe),
    retryDelay(MONITOR_RETRY_DELAY),  is_stop(false), sem_send(0), sem_acked(0),  sem_resend(0)
{
    retryDelay = GetArg("-blockmon_retry_delay", MONITOR_RETRY_DELAY);
}


void COKBlockChainMonitor::BuildEvent(const int &action, const CTransaction& tx, const CNode *pfrom){
    if(tx.IsNull())
        return;


    int64_t now = 0;
    now = GetAdjustedTime();

    const std::string txHash = tx.GetHash().ToString();
    COKLogEvent logEvent(OC_TYPE_TX, action, txHash, pfrom == NULL ? "oklink.com" : pfrom->addr.ToStringIP());

    uint256 uuid = NewRandomUUID();
    string requestId = "event-" + NewRequestId(now, uuid);

    if(!WriteCacheEvent(now, uuid, logEvent))
    {
        //TODO
        LogPrintf("ok-- COKBlockChainMonitor:WriteCacheEvent fail event: %s\n",logEvent.ToString());
    }

    push_send(requestId, logEvent);

}

void COKBlockChainMonitor::BuildEvent(const int &action, const CBlock *pblock, const CNode *pfrom){
   int64_t now = 0;
   now = GetAdjustedTime();


   const std::string blockHash = pblock->GetHash().ToString();
   COKLogEvent logEvent(OC_TYPE_BLOCK, action, blockHash, pfrom == NULL ? "oklink.com" : pfrom->addr.ToStringIP() );

   uint256 uuid = NewRandomUUID();
   string requestId = "event-" + NewRequestId(now, uuid);

   if(!WriteCacheEvent(now, uuid, logEvent))
   {
       //TODO
       LogPrintf("ok-- COKBlockChainMonitor:WriteCacheEvent fail event: %s\n",logEvent.ToString());
   }

   push_send(requestId, logEvent);
}


void COKBlockChainMonitor::SyncTransaction(const CTransaction &tx, const CBlock *pblock, const CNode *pfrom, bool fConflicted)
{
    if(pblock)
    {
        //只同步未确认tx，如pblock为被确认tx,
        return;
    }

   LogPrintf("tx_monitor SyncTransaction:%s, fConflicted:%d\n",tx.GetHash().ToString(), fConflicted);
   BuildEvent(fConflicted == true ? OC_ACTION_ORPHANE:OC_ACTION_NEW, tx, pfrom);
}


/**
 * @brief TransactionMonitor::SyncConnectBlock block生效遍历所有tx
 * @param pblock
 * @param pindex
 * @param addresses
 */
void COKBlockChainMonitor::SyncConnectBlock(const CBlock *pblock, const CBlockIndex* pindex, const CNode *pfrom)
{
     LogPrintf("tx_monitor SyncConnectBlock:%s\n",pblock->GetHash().ToString());
//        BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
//        {
//            BuildEvent(OC_ACTION_CONFIRM, tx);  //确认
//        }
     BuildEvent(OC_ACTION_NEW,pblock, pfrom);
}

void COKBlockChainMonitor::SyncDisconnectBlock(const CBlock *pblock)
{
    LogPrintf("tx_monitor dis_ConnectBlock:%s\n",pblock->GetHash().ToString());

   // （不再写记录）在冲突tx中记录OC_ACTION_ORPHANE事件
//        BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
//        {
//             BuildEvent(OC_ACTION_ORPHANE, tx);  //孤立，
//        }
        BuildEvent(OC_ACTION_ORPHANE,pblock);
}

//从leveldb加载缓存tx events
bool COKBlockChainMonitor::LoadCacheEvents()
{

    leveldb::Iterator *pcursor = NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair('T', make_pair(int64_t(0), uint256()));
    pcursor->Seek(ssKeySet.str());

    queue<pair<pair<int64_t, uint256>, COKLogEvent> > cacheEventQueue;

    //Load Blocks
    while(pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        try
        {
            leveldb::Slice slKey = pcursor->key(); //key中包含timestamp，uuid信息
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if(chType == 'T')
            {
                leveldb::Slice slValue = pcursor->value();  //value为logEvent信息
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);

                int64_t timestamp;
                ssKey >> timestamp;

                uint256 uuid;
                ssKey >> uuid;

                COKLogEvent logEvent;
                ssValue >> logEvent;

                if(!logEvent.IsNull()){
                    LogPrintf("ok-- loadEvent:%s\n", logEvent.ToString());
                    cacheEventQueue.push(make_pair(make_pair(timestamp, uuid), logEvent));
                }

            }
            else
            {
                break;
            }

            pcursor->Next();
        }
        catch(std::exception &e)
        {
            throw runtime_error(strprintf("%s : Deserialize or I/O error - %s", __func__, e.what()));
        }
    }
    delete pcursor;

    threadGroup.create_thread(boost::bind(&COKBlockChainMonitor::PushCacheLogEvents, this, cacheEventQueue));

    return true;
}

bool COKBlockChainMonitor::WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid, const COKLogEvent& logEvent)
{
    bool ret = Write(std::make_pair('T', std::make_pair(timestamp, uuid)), logEvent, false);
//    LogPrintf("ok-- COKBlockChainMonitor:write %lld, ret=%d\n", timestamp,ret);
    return ret;
}

bool COKBlockChainMonitor::DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid)
{
    bool ret =  Erase(std::make_pair('T', std::make_pair(timestamp, uuid)), false);
//    LogPrintf("ok-- COKBlockChainMonitor:delete requestId: %lld, ret=%d\n",timestamp, ret);
    return ret;
}

void COKBlockChainMonitor::Start()
{
    if(!LoadCacheEvents())
    {
        throw runtime_error("COKBlockChainMonitor LoadEvent fail!");
    }

    threadGroup.create_thread(boost::bind(&COKBlockChainMonitor::SendThread, this));
    threadGroup.create_thread(boost::bind(&COKBlockChainMonitor::AckThread, this));
    threadGroup.create_thread(boost::bind(&COKBlockChainMonitor::ResendThread, this));
}

void COKBlockChainMonitor::Stop()
{
    is_stop = true;
    sem_send.post();
    sem_acked.post();
    sem_resend.post();

//    ioService.stop();
    threadGroup.interrupt_all();
    threadGroup.join_all();
}


const uint256 COKBlockChainMonitor::NewRandomUUID() const
{
    uint256 uuid;
    RandAddSeedPerfmon();

    RAND_bytes(uuid.begin(), uuid.end() - uuid.begin());
    return uuid;
}

const std::string COKBlockChainMonitor::NewRequestId(const int64_t &now, const uint256 &uuid) const
{
    CDataStream ssId(SER_DISK, CLIENT_VERSION);
    ssId << now;
    ssId << uuid;

    return EncodeBase58((const unsigned char*)&ssId[0], (const unsigned char*)&ssId[0] + (int)ssId.size());
}

bool COKBlockChainMonitor::decodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid)
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

bool COKBlockChainMonitor::decodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid)
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

const std::string COKBlockChainMonitor::NewRequestId() const
{
    const int64_t now = GetAdjustedTime();
    const uint256 uuid = NewRandomUUID();

    return NewRequestId(now, uuid);
}

void COKBlockChainMonitor::push_send(const std::string &requestId, const COKLogEvent& logEvent)
{
    LOCK2(cs_map, cs_send);
//     LogPrintf("ok-- COKBlockChainMonitor:push_send: %s\n",logEvent.ToString());
    requestMap.insert(make_pair(requestId, logEvent));
    sendQueue.push(requestId);

    sem_send.post();
}

void COKBlockChainMonitor::push_acked(const std::string &requestId)
{
    LOCK(cs_acked);
//     LogPrintf("ok-- CEventMonitor:push_acked requestId: %s\n",requestId);
    ackedQueue.push(requestId);

    sem_acked.post();
}

void COKBlockChainMonitor::push_resend(const std::string &requestId)
{
    LOCK(cs_resend);
//    LogPrintf("ok-- CEventMonitor:push_resend requestId: %s\n",requestId);
    resendQueue.push(make_pair(requestId, GetAdjustedTime() + retryDelay));

    sem_resend.post();
}

bool COKBlockChainMonitor::pull_send(std::string &requestId, const COKLogEvent ** const logEvent)
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

bool COKBlockChainMonitor::pull_acked(std::string &requestId, const COKLogEvent ** const logEvent)
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

bool COKBlockChainMonitor::pull_resend(std::string &requestId, const COKLogEvent ** const logEvent)
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



void COKBlockChainMonitor::CallOKLogEvent(const std::string &requestId, const COKLogEvent& logEvent){
    if(logEvent.IsNull())
        return;


    int ret = OKCoin_Log_Event(logEvent);
    if(ret > 0){
        push_acked(requestId);
    }
    else{
        push_resend(requestId);
    }
}

void COKBlockChainMonitor::SendThread()
{
    RenameThread("bitcoin-block-monitor-send");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(1 * 1000);
    LogPrintf("CEventMonitor:SendThread ...\n");
    while(!is_stop)
    {
        string requestId;
        const COKLogEvent * logEvent;

        if(!pull_send(requestId, &logEvent))
        {
            MilliSleep(1);
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
     LogPrintf("COKBlockChainMonitor:SendThread end...\n");
}

void COKBlockChainMonitor::AckThread()
{
    RenameThread("bitcoin-block-monitor-ack");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(2 * 1000);

    while(!is_stop)
    {
        string requestId;
        const COKLogEvent * logEvent;

        if(!pull_acked(requestId, &logEvent))
        {
            MilliSleep(1);
            continue;
        }

        try
        {
            if(do_acked(requestId))
                LogPrintf("ok-- do_acked success -> requestId: %s\n",requestId);
            else
               LogPrintf("ok-- do_acked fail -> requestId: %s\n",requestId);
        }
        catch(std::exception &e)
        {
            LogException(&e, string("COKBlockChainMonitor::AckThread() -> "+string(e.what())).c_str());
            LogPrintf("COKBlockChainMonitor::AckThread() ->%s\n",string(e.what()));
        }
        catch(...)
        {
            LogPrintf("COKBlockChainMonitor::AckThread() -> unknow exception\n");
        }
    }
}

void COKBlockChainMonitor::ResendThread()
{
    RenameThread("bitcoin-block-monitor-resend");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(3 * 1000);

    while(!is_stop)
    {
        string requestId;
        const COKLogEvent * logEvent;

        if(!pull_resend(requestId, &logEvent))
        {
            MilliSleep(50);
            continue;
        }

        try
        {
            do_resend(requestId, *logEvent);
        }
        catch(std::exception &e)
        {
            LogException(&e, string("COKBlockChainMonitor::ResendThread() -> "+string(e.what())).c_str());
            LogBlock("COKBlockChainMonitor::ResendThread() -> "+string(e.what())+"\n");
        }
        catch(...)
        {
            LogBlock("COKBlockChainMonitor::ResendThread() -> unknow exception\n");
        }
    }
}

//检测httppost无响应（ack）requestId,并放入重发队列
void COKBlockChainMonitor::NoResponseCheckThread()
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
            LogException(&e, string("COKBlockChainMonitor::NoResponseCheckThread() -> "+string(e.what())).c_str());
            LogBlock("COKBlockChainMonitor::NoResponseCheckThread() -> "+string(e.what())+"\n");
        }
        catch(...)
        {
            LogBlock("COKBlockChainMonitor::NoResponseCheckThread() -> unknow exception\n");
        }

        MilliSleep(retryDelay * 1000);
    }
}

bool COKBlockChainMonitor::do_send(const std::string &requestId, const COKLogEvent& logEvent)
{
//    LogPrintf("do_send -> requestId: %s, event:%s\n",requestId,logEvent.ToString());
    CallOKLogEvent(requestId, logEvent);
    return true;
}

bool COKBlockChainMonitor::do_acked(const std::string &requestId)
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
//    LogPrintf("ok-- do_acked requestId:%s", requestId);
    return  DeleteCacheEvent(timestamp, uuid);
}

bool COKBlockChainMonitor::do_resend(const std::string &requestId, const COKLogEvent& logEvent)
{
     LogPrintf("do_resend -> requestId: %s, event:%s\n",requestId,logEvent.ToString());
    //ioService.post(boost::bind(CallOKLogEvent, this, requestId, logEvent));
     CallOKLogEvent(requestId, logEvent);
    return true;
}

void COKBlockChainMonitor::NoResponseCheck()
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

void COKBlockChainMonitor::PushCacheLogEvents(std::queue<std::pair<std::pair<int64_t, uint256>, COKLogEvent> > &cachedEventQueue)
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

