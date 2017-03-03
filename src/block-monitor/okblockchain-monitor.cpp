#include "okblockchain-monitor.h"

#include "serialize.h"
#include "util.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"
#include "random.h"
#include "mysql_wrapper/okcoin_log.h"

#define MONITOR_RETRY_DELAY	60
#define SQL_WRITE_THREAD_COUNT   8

using namespace std;
using namespace boost;
//using namespace json_spirit;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;

static boost::asio::io_service ioService;
static boost::asio::io_service::work work(ioService);


static void io_service_run(void)
{
    ioService.run();
}


COKBlockChainMonitor::COKBlockChainMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    CLevelDBWrapper(GetDataDir() / "blocks" / "monitx", nCacheSize, fMemory, fWipe),
    retryDelay(MONITOR_RETRY_DELAY),  is_stop(false), sem_send(0), sem_acked(0),  sem_resend(0)
{
    retryDelay = GetArg("-blockmon_retry_delay", MONITOR_RETRY_DELAY);
}


void COKBlockChainMonitor::BuildEvent(const int &action, const CTransaction& tx,  CNode *pfrom){
    if(tx.IsNull())
        return;


    int64_t now = 0;
    now = GetAdjustedTime();

    const std::string txHash = tx.GetHash().ToString();
    COKLogEvent logEvent(OC_TYPE_TX, action, txHash, pfrom ? pfrom->addr.ToStringIP(): "oklink.com");
    uint256 uuid = NewRandomUUID();
    string requestId = "event-" + NewRequestId(now, uuid);

//    if(!WriteCacheEvent(now, uuid, logEvent))
//    {
//        //TODO
//        LogPrintf("ok-- COKBlockChainMonitor:WriteCacheEvent fail event: %s\n",logEvent.ToString());
//    }

    push_send(requestId, logEvent);

}

void COKBlockChainMonitor::BuildEvent(const int &action, const CBlock *pblock, CNode *pfrom){
   int64_t now = 0;
   now = GetAdjustedTime();


   const std::string blockHash = pblock->GetHash().ToString();

   COKLogEvent logEvent(OC_TYPE_BLOCK, action, blockHash, pfrom ? pfrom->addr.ToStringIP(): "oklink.com");
   uint256 uuid = NewRandomUUID();
   string requestId = "event-" + NewRequestId(now, uuid);

   if(!WriteCacheEvent(now, uuid, logEvent))
   {
       //TODO
       LogPrintf("okcoin_log-- COKBlockChainMonitor:WriteCacheEvent fail event: %s\n",logEvent.ToString());
   }

   push_send(requestId, logEvent);
}


void COKBlockChainMonitor::SyncTransaction(const CTransaction &tx, const CBlock *pblock, CNode *pfrom, bool fConflicted)
{
    if(pblock)
    {
        //只同步未确认tx，如pblock为被确认tx,
        return;
    }

   LogPrintf("okcoin_log  (receive) SyncTransaction:%s, fConflicted:%d\n",tx.GetHash().ToString(), fConflicted);
   BuildEvent(fConflicted == true ? OC_ACTION_ORPHANE:OC_ACTION_NEW, tx, pfrom);
}


/**
 * @brief TransactionMonitor::SyncConnectBlock block生效遍历所有tx
 * @param pblock
 * @param pindex
 * @param addresses
 */
void COKBlockChainMonitor::SyncConnectBlock(const CBlock *pblock, const CBlockIndex* pindex,  CNode *pfrom)
{
     LogPrintf("okcoin_log (receive) SyncConnectBlock:%s\n",pblock->GetHash().ToString());
//        BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
//        {
//            BuildEvent(OC_ACTION_CONFIRM, tx);  //确认
//        }
     BuildEvent(OC_ACTION_NEW, pblock, pfrom);
}

void COKBlockChainMonitor::SyncDisconnectBlock(const CBlock *pblock)
{
    LogPrintf("okcoin_log dis_ConnectBlock:%s\n",pblock->GetHash().ToString());

   // （不再写记录）在冲突tx中记录OC_ACTION_ORPHANE事件
//        BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
//        {
//             BuildEvent(OC_ACTION_ORPHANE, tx);  //孤立，
//        }
        BuildEvent(OC_ACTION_ORPHANE,pblock);
}



bool COKBlockChainMonitor::WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid, const COKLogEvent& logEvent)
{
    return Write(std::make_pair('T', std::make_pair(timestamp, uuid)), logEvent, false);
}

bool COKBlockChainMonitor::DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid)
{
    return  Erase(std::make_pair('T', std::make_pair(timestamp, uuid)), false);
}

void COKBlockChainMonitor::Start()
{
    if(!LoadCacheEvents())
    {
        // do nothing
    }

    for(int i = 0; i < SQL_WRITE_THREAD_COUNT; i++)
    {
        threadGroup.create_thread(boost::bind(&io_service_run));
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

    for(int i = 0; i < SQL_WRITE_THREAD_COUNT; i++){
        ioService.stop();
    }

    threadGroup.interrupt_all();
    threadGroup.join_all();
}


const uint256 COKBlockChainMonitor::NewRandomUUID() const
{
    uint256 uuid;
    RandAddSeed();

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
    requestMap.insert(make_pair(requestId, logEvent));
    sendQueue.push(requestId);

    sem_send.post();
}

void COKBlockChainMonitor::push_acked(const std::string &requestId)
{
    LOCK(cs_acked);
    ackedQueue.push(requestId);
    sem_acked.post();
}

void COKBlockChainMonitor::push_resend(const std::string &requestId)
{
    LOCK(cs_resend);
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
            return false;
        }

        *logEvent = &it->second;
    }

//    {
//        LOCK(cs_sendMap);
//        sendMap.insert(make_pair(requestId, GetAdjustedTime() + retryDelay));
//    }

    return true;
}

bool COKBlockChainMonitor::pull_acked(std::string &requestId /*, const COKLogEvent ** const logEvent*/)
{
    sem_acked.wait();
    if(is_stop)
    {
        return false;
    }

    LOCK(cs_acked);

    requestId = ackedQueue.front();
    ackedQueue.pop();

//    boost::unordered_map<string, COKLogEvent>::const_iterator it = requestMap.find(requestId);
//    if(it == requestMap.end())
//    {
//        LogBlock("pull_acked can not find request in map: "+requestId+"\n");
//        return false;
//    }
//        *logEvent = &it->second;

    return true;
}

bool COKBlockChainMonitor::pull_resend(std::string &requestId, const COKLogEvent ** const logEvent)
{
    sem_resend.wait();

    if(is_stop)
    {
        return false;
    }

    LOCK2(cs_map, cs_resend);

    const int64_t now = GetAdjustedTime();

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
        return false;
    }

    *logEvent = &it->second;

    return true;
}


static void CallOKLogEventWrappedException(COKBlockChainMonitor* self, const std::string &requestId, const COKLogEvent& logEvent){
    self->CallOKLogEvent(requestId, logEvent);
}


void COKBlockChainMonitor::CallOKLogEvent(const std::string &requestId, const COKLogEvent& logEvent){
    if(logEvent.IsNull())
        return;


    int ret = OKCoin_Log_Event(logEvent);
    if(ret > 0){
        LogPrintf("ok true-- CallOKLogEvent requestid:%s\n",requestId);
        push_acked(requestId);
    }
    else{
        LogPrintf("ok false-- CallOKLogEvent requestid:%s\n",requestId);
        push_resend(requestId);
    }
}

void COKBlockChainMonitor::SendThread()
{
    RenameThread("bitcoin-block-monitor-send");
    LogPrintf("CEventMonitor:SendThread ...\n");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(1 * 1000);

    while(!is_stop)
    {
        string requestId;
        const COKLogEvent *logEvent;

        if(!pull_send(requestId, &logEvent))
        {
            MilliSleep(200);
            continue;
        }

        try
        {
            do_send(requestId, *logEvent);
        }
        catch(std::exception &e)
        {
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
        if(!pull_acked(requestId))
        {
            MilliSleep(500);
            continue;
        }

        try
        {
            bool ret = do_acked(requestId);
            LogPrintf("ok-- do_acked (%d)  ->    requestid: %s\n", ret,requestId);
        }
        catch(std::exception &e)
        {
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
            MilliSleep(1000);
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

//检测无响应（ack）requestId,并放入重发队列
//void COKBlockChainMonitor::NoResponseCheckThread()
//{
//    RenameThread("bitcoin-block-monitor-NoResponseCheck");

//    static bool fOneThread;
//    if (fOneThread)
//    {
//        return;
//    }
//    fOneThread = true;

//    MilliSleep(retryDelay * 1000);

//    while(true)
//    {
//        try
//        {
//            NoResponseCheck();
//        }
//        catch(std::exception &e)
//        {
//            LogException(&e, string("COKBlockChainMonitor::NoResponseCheckThread() -> "+string(e.what())).c_str());
//            LogBlock("COKBlockChainMonitor::NoResponseCheckThread() -> "+string(e.what())+"\n");
//        }
//        catch(...)
//        {
//            LogBlock("COKBlockChainMonitor::NoResponseCheckThread() -> unknow exception\n");
//        }

//        MilliSleep(retryDelay * 1000);
//    }
//}

bool COKBlockChainMonitor::do_send(const std::string &requestId, const COKLogEvent& logEvent)
{
//    LogPrintf("do_send -> requestId: %s, event:%s\n",requestId,logEvent.ToString());

//    CallOKLogEvent(requestId, logEvent);
    ioService.post(boost::bind(CallOKLogEventWrappedException,this,requestId,logEvent));
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
        LOCK(cs_map);
        requestMap.erase(requestId);
    }
    return  DeleteCacheEvent(timestamp, uuid);
}

bool COKBlockChainMonitor::do_resend(const std::string &requestId, const COKLogEvent& logEvent)
{
     LogPrintf("do_resend -> requestId: %s, event:%s\n",requestId,logEvent.ToString());
   ioService.post(boost::bind(CallOKLogEventWrappedException,this,requestId,logEvent));
//     CallOKLogEvent(requestId, logEvent);
    return true;
}



//从leveldb加载缓存tx events
bool COKBlockChainMonitor::LoadCacheEvents()
{

    leveldb::Iterator *pcursor = NewIterator();

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair('T', make_pair(int64_t(0), uint256()));
    pcursor->Seek(ssKeySet.str());

    static queue<pair<pair<int64_t, uint256>, COKLogEvent> > cacheEventQueue;

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
            LogPrintf("ok--LoadCacheEvents : Deserialize or I/O error - %s", e.what());
        }

    }
    delete pcursor;

    threadGroup.create_thread(boost::bind(&COKBlockChainMonitor::PushCacheLogEvents, this, cacheEventQueue));

    return true;
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

    MilliSleep(1 * 1000);

    while(!cachedEventQueue.empty())
    {
        pair<pair<int64_t, uint256>,  COKLogEvent> request = cachedEventQueue.front();
        const COKLogEvent &logEvent = request.second;
        const int64_t &now = request.first.first;
        const uint256 &uuid = request.first.second;
        const string requestId = "event-" + NewRequestId(now, uuid);
        push_send(requestId, logEvent);
        cachedEventQueue.pop();
    }
}

