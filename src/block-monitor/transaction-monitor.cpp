#include "transaction-monitor.h"

#include "serialize.h"
#include "util.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"

#include "mysql_wrapper/okcoin_log.h"

using namespace std;
using namespace boost;
using namespace json_spirit;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;

TransactionMonitor::TransactionMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    CEventMonitor(GetDataDir() / "blocks" / "monitx", nCacheSize, fMemory, fWipe)
{

}

void TransactionMonitor::BuildEvent(const int &action, const CTransaction& tx){
    if(tx.IsNull())
        return;


    int64_t now = 0;
    now = GetAdjustedTime();

    const std::string txHash = tx.GetHash().ToString();
    COKLogEvent logEvent(OC_TYPE_TX, action, txHash);

    uint256 uuid = NewRandomUUID();
    string requestId = "event-" + NewRequestId(now, uuid);

    if(!WriteCacheEvent(now, uuid, logEvent))
    {
        //TODO
    }

    push_send(requestId, logEvent);

}

void TransactionMonitor::BuildEvent(const int &action, const CBlock *pblock){
   int64_t now = 0;
   now = GetAdjustedTime();


   const std::string blockHash = pblock->GetHash().ToString();
   COKLogEvent logEvent(OC_TYPE_BLOCK, action, blockHash);

   uint256 uuid = NewRandomUUID();
   string requestId = "event-" + NewRequestId(now, uuid);

   if(!WriteCacheEvent(now, uuid, logEvent))
   {
       //TODO
   }

   push_send(requestId, logEvent);
}


void TransactionMonitor::SyncTransaction(const CTransaction &tx, const CBlock *pblock, const boost::unordered_map<uint160, std::string> &addresses)
{
    if(pblock)
    {
        //只同步未确认tx，如pblock为被确认tx,
        return;
    }

    LogPrintf("tx_monitor SyncTransaction:%s\n",tx.GetHash().ToString());
   BuildEvent(OC_ACTION_NEW, tx);
}


/**
 * @brief TransactionMonitor::SyncConnectBlock block生效遍历所有tx
 * @param pblock
 * @param pindex
 * @param addresses
 */
void TransactionMonitor::SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses)
{
     LogPrintf("tx_monitor SyncConnectBlock:%s\n",pblock->GetHash().ToString());
//        BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
//        {
//            BuildEvent(OC_ACTION_CONFIRM, tx);  //确认
//        }
     BuildEvent(OC_ACTION_NEW,pblock);
}

void TransactionMonitor::SyncDisconnectBlock(const CBlock *pblock)
{
    LogPrintf("tx_monitor dis_ConnectBlock:%s\n",pblock->GetHash().ToString());

        BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
        {
             BuildEvent(OC_ACTION_ORPHANE, tx);  //孤立
        }

        BuildEvent(OC_ACTION_ORPHANE,pblock);

}

//从leveldb加载缓存tx events
bool TransactionMonitor::LoadCacheEvents()
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

                if(!logEvent.IsNull())
                    cacheEventQueue.push(make_pair(make_pair(timestamp, uuid), logEvent));

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

    threadGroup.create_thread(boost::bind(&TransactionMonitor::PushCacheLogEvents, this, cacheEventQueue));

    return true;
}

bool TransactionMonitor::WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid, const COKLogEvent& logEvent)
{
    return Write(std::make_pair('T', std::make_pair(timestamp, uuid)), logEvent, true);
}

bool TransactionMonitor::DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid)
{
    return Erase(std::make_pair('T', std::make_pair(timestamp, uuid)), true);
}

