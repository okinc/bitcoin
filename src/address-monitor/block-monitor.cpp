/*
 * block-monitor.cpp
 *
 *  Created on: 2015年3月26日
 *      Author: Administrator
 */


#include "block-monitor.h"
#include "okhttpclient.h"
#include "serialize.h"
#include "util.h"
#include "base58.h"
#include "timedata.h"
#include "script/script.h"
#include "main.h"

#include <sstream>


#include "univalue.h"

using namespace std;
using namespace boost;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;



static void io_service_run(BlockMonitor *self)
{
    self->Run_io_service();
}


static UniValue BuildValue(const CBlock *pblock,
        const string &blockHash, const int nHeight, const int64_t &time, const int status)
{
     UniValue object(UniValue::VOBJ);

    object.push_back(Pair("time", time));
    object.push_back(Pair("coinType", 1));
    if(pblock)
    {
        object.push_back(Pair("blockHeight", nHeight));
        object.push_back(Pair("blockHash", blockHash));
    }
    object.push_back(Pair("status", status));

    return object;
}


BlockMonitor::BlockMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    COKMonitor(GetDataDir() / "blocks" / "blockmon", nCacheSize, fMemory, fWipe)
{
	retryDelay = GetArg("-blockmon_retry_delay", BLOCKMON_RETRY_DELAY);
	httpPool = GetArg("-blockmon_http_pool", BLOCKMON_HTTP_POOL);
    static boost::asio::io_service::work threadPool(ioService);
}

BlockMonitor::~BlockMonitor()
{

}


void BlockMonitor::Start()
{
    if(!LoadBlocks())
    {
        throw runtime_error("BlockMonitor LoadTransactions fail!");
    }

    for(int i = 0; i < httpPool; i++)
    {
        threadGroup.create_thread(boost::bind(&io_service_run, this));
    }

    threadGroup.create_thread(boost::bind(&BlockMonitor::PostThread, this));
    threadGroup.create_thread(boost::bind(&BlockMonitor::AckThread, this));
    threadGroup.create_thread(boost::bind(&BlockMonitor::ResendThread, this));
    threadGroup.create_thread(boost::bind(&BlockMonitor::NoResponseCheckThread, this));
}


bool BlockMonitor::LoadBlocks()
{
    CDBIterator *pcursor = NewIterator();

	CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair('B', make_pair(int64_t(0), uint256()));
	pcursor->Seek(ssKeySet.str());

	queue<pair<pair<int64_t, uint256>, pair<int, string> > > syncConnectQueue;
	queue<pair<pair<int64_t, uint256>, pair<int, string> > > syncDisconnectQueue;

	//Load addresses
	while(pcursor->Valid())
	{
		boost::this_thread::interruption_point();
		try
		{

            std::pair<char, pair<int64_t, uint256> > ssKey;
            if (pcursor->GetKey(ssKey)) {
                if(ssKey.first == 'B')
                {
                    int64_t timestamp = ssKey.second.first;
                    uint256 uuid = ssKey.second.second;

                    std::pair<int, string> ssValue;
                    if (pcursor->GetValue(ssValue)) {
                        int type = ssValue.first;
                        std::string json = ssValue.second;

                        if(type == SYNC_CONNECT)
                        {
                            syncConnectQueue.push(make_pair(make_pair(timestamp, uuid), make_pair(type, json)));
                        }
                        else if(type == SYNC_DISCONNECT)
                        {
                            syncDisconnectQueue.push(make_pair(make_pair(timestamp, uuid), make_pair(type, json)));
                        }
                    }
                }
                else
                {
                    break;
                }
            }
			pcursor->Next();
		}
		catch(std::exception &e)
		{
//			throw runtime_error(strprintf("%s : Deserialize or I/O error - %s", __func__, e.what()));
		}
	}
	delete pcursor;

	threadGroup.create_thread(boost::bind(&BlockMonitor::LoadSyncConnect, this, syncConnectQueue));
	threadGroup.create_thread(boost::bind(&BlockMonitor::LoadSyncDisconnect, this, syncDisconnectQueue));

	return true;
}


bool BlockMonitor::WriteBlock(const int64_t &timestamp, const uint256 &uuid, const int type, const std::string &json)
{
	return Write(std::make_pair('B', std::make_pair(timestamp, uuid)), std::make_pair(type, json), true);
}

bool BlockMonitor::DeleteBlock(const int64_t &timestamp, const uint256 &uuid)
{
	return Erase(std::make_pair('B', std::make_pair(timestamp, uuid)), true);
}


void BlockMonitor::SyncConnectBlock(const CBlock *pblock, CBlockIndex* pindex, const boost::unordered_map<uint160, std::string> &addresses)
{
    UniValue ret(UniValue::VOBJ);
	int64_t now = 0;

	{
		now = GetAdjustedTime();
		const string blockHash = pblock->GetHash().GetHex();
		const int nHeight = pindex->nHeight;
		const int status = 1;

        ret = BuildValue(pblock, blockHash, nHeight, now, status);
	}

	uint256 uuid = NewRandomUUID();
	string requestId = "conn-" + NewRequestId(now, uuid);

    UniValue result(UniValue::VOBJ);
	result.push_back(Pair("requestId", requestId));
	result.push_back(Pair("content", ret));
    string json = result.write(0, 0);

	if(!WriteBlock(now, uuid, SYNC_CONNECT, json))
	{
		//TODO
	}

    Push_post(requestId, json);
}

void BlockMonitor::SyncDisconnectBlock(const CBlock *pblock)
{
     UniValue ret(UniValue::VOBJ);
	int64_t now = 0;

	{
		now = GetAdjustedTime();
		string blockHash = pblock->GetHash().GetHex();
		int nHeight = -2;
		const int status = -2;

        ret = BuildValue(pblock, blockHash, nHeight, now, status);
	}

	uint256 uuid = NewRandomUUID();
	string requestId = "dis-" + NewRequestId(now, uuid);

    UniValue result(UniValue::VOBJ);
	result.push_back(Pair("requestId", requestId));
	result.push_back(Pair("content", ret));
    string json = result.write(0, 0);

	if(!WriteBlock(now, uuid, SYNC_DISCONNECT, json))
	{
		//TODO
	}

    Push_post(requestId, json);
}

void BlockMonitor::PostActionWrappedException(const std::string &requestId, const std::string& body)
{

    try
    {
        LogBlock("PostAction requestId:" + requestId + "\n body:" +body +"\n");
        const string host = GetArg("-blockmon_host", "127.0.0.1");
        const int port = GetArg("-blockmon_port", 80);
        const string url = GetArg("-blockmon_url", "");

        UniValue reply = CallHttpPost(host, port, url, body);
        if (reply.getValStr() == "true") {
            Push_acked(requestId);
            LogBlock("success reply -> requestId: "+requestId+"\n");
        } else {
            LogBlock("wrong reply -> requestId: "+requestId+", reply: "+reply.getValStr()+"\n");
        }
    }
    catch(const std::exception& e)
    {
        LogBlock("exception in CallHttpPost -> "+string(e.what())+"\n");
    }
    catch(...)
    {
        LogBlock("unknow exception in CallHttpPost\n");
    }

}

bool BlockMonitor::AckWrappedExceptioin(const int64_t &timestamp, const uint256 &uuid)
{
    DeleteBlock(timestamp, uuid);
}


void BlockMonitor::PostThread()
{
    RenameThread("bitcoin-block-monitor-post");

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
		const string * pjson;

        if(!Pull_post(requestId, &pjson))
		{
			MilliSleep(1000);
			continue;
		}

		try
		{
            Do_post(requestId, pjson);
		}
		catch(std::exception &e)
		{
	    	LogException(&e, string("BlockMonitor::PostThread() -> "+string(e.what())).c_str());
	    	LogBlock("BlockMonitor::PostThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogBlock("BlockMonitor::PostThread() -> unknow exception\n");
		}
	}
}

void BlockMonitor::AckThread()
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
		const string * pjson;
		string json;

        if(!Pull_acked(requestId, &pjson))
		{
			MilliSleep(1000);
			continue;
		}
		json = *pjson;

		try
		{
            if(Do_acked(requestId))
				LogBlock("do_acked success -> requestId: "+requestId+", json: "+json+"\n");
			else
				LogBlock("do_acked fail -> requestId: "+requestId+", json: "+json+"\n");
		}
		catch(std::exception &e)
		{
	    	LogException(&e, string("BlockMonitor::AckThread() -> "+string(e.what())).c_str());
	    	LogBlock("BlockMonitor::AckThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogBlock("BlockMonitor::AckThread() -> unknow exception\n");
		}
	}
}

void BlockMonitor::ResendThread()
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
		const string * pjson;

        if(!Pull_resend(requestId, &pjson))
		{
			MilliSleep(1000);
			continue;
		}

		try
		{
            Do_resend(requestId, pjson);
		}
		catch(std::exception &e)
		{
	    	LogException(&e, string("BlockMonitor::ResendThread() -> "+string(e.what())).c_str());
	    	LogBlock("BlockMonitor::ResendThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogBlock("BlockMonitor::ResendThread() -> unknow exception\n");
		}
	}
}

void BlockMonitor::NoResponseCheckThread()
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
	    	LogException(&e, string("BlockMonitor::NoResponseCheckThread() -> "+string(e.what())).c_str());
	    	LogBlock("BlockMonitor::NoResponseCheckThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogBlock("BlockMonitor::NoResponseCheckThread() -> unknow exception\n");
		}

		MilliSleep(retryDelay * 1000);
	}
}


void BlockMonitor::LoadSyncConnect(queue<pair<pair<int64_t, uint256>, pair<int, string> > > &syncConnectQueue)
{
    RenameThread("bitcoin-block-monitor-LoadSyncConnect");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

	while(!syncConnectQueue.empty())
    {
		pair<pair<int64_t, uint256>, pair<int, string> > request = syncConnectQueue.front();
		syncConnectQueue.pop();
		const string &json = request.second.second;
		const int64_t &now = request.first.first;
		const uint256 &uuid = request.first.second;
		const string requestId = "conn-" + NewRequestId(now, uuid);

        Push_post(requestId, json);
    }
}

void BlockMonitor::LoadSyncDisconnect(queue<pair<pair<int64_t, uint256>, pair<int, string> > > &syncDisconnectQueue)
{
    RenameThread("bitcoin-block-monitor-LoadSyncDisconnect");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

	while(!syncDisconnectQueue.empty())
    {
		pair<pair<int64_t, uint256>, pair<int, string> > request = syncDisconnectQueue.front();
		syncDisconnectQueue.pop();
		const string &json = request.second.second;
		const int64_t &now = request.first.first;
		const uint256 &uuid = request.first.second;
		const string requestId = "dis-" + NewRequestId(now, uuid);

        Push_post(requestId, json);
    }
}

