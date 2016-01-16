/*
 * block-monitor.cpp
 *
 *  Created on: 2015年3月26日
 *      Author: Administrator
 */

#include "serialize.h"
#include "util.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"

#include "block-monitor.h"
//#include "mysql_wrapper/okcoin_log.h"

using namespace std;
using namespace boost;
using namespace json_spirit;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;

/*
static boost::asio::io_service ioService;
static boost::asio::io_service::work threadPool(ioService);

static void io_service_run(void)
{
	ioService.run();
}
*/

BlockMonitor::BlockMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    CEventMonitor(GetDataDir() / "blocks" / "moniblock", nCacheSize, fMemory, fWipe)
{

}

BlockMonitor::~BlockMonitor(){

}

//从leveldb加载缓存blocks events
bool BlockMonitor::LoadCacheEvents()
{
	leveldb::Iterator *pcursor = NewIterator();

	CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
	ssKeySet << make_pair('B', make_pair(int64_t(0), uint256()));
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
			if(chType == 'B')
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

    threadGroup.create_thread(boost::bind(&BlockMonitor::PushCacheLogEvents, this, cacheEventQueue));

	return true;
}


bool BlockMonitor::WriteCacheEvent(const int64_t &timestamp, const uint256 &uuid, const COKLogEvent& logEvent)
{
    return Write(std::make_pair('B', std::make_pair(timestamp, uuid)), logEvent, true);
}

bool BlockMonitor::DeleteCacheEvent(const int64_t &timestamp, const uint256 &uuid)
{
	return Erase(std::make_pair('B', std::make_pair(timestamp, uuid)), true);
}



 void BlockMonitor::BuildEvent(const int &action, const CBlock *pblock){
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

void BlockMonitor::SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses)
{
    if(pblock){
         LogPrintf("block_monitor syncConnectBlock:%s\n",pblock->GetHash().ToString());
         BuildEvent(OC_ACTION_NEW, pblock);
    }
}

void BlockMonitor::SyncDisconnectBlock(const CBlock *pblock)
{
    if(pblock){
         LogPrintf("block_monitor dis_ConnectBlock:%s\n",pblock->GetHash().ToString());
         BuildEvent(OC_ACTION_ORPHANE, pblock);
    }
}




