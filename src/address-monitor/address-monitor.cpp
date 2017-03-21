#include "address-monitor/address-monitor.h"
#include "serialize.h"
#include "util.h"
#include "timedata.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"

#include "okhttpclient.h"

using namespace std;
using namespace boost;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;

//static boost::asio::io_service ioService;
//static boost::asio::io_service::work threadPool(ioService);

static void io_service_run(AddressMonitor *self)
{
    self->Run_io_service();
}


AddressMonitor::AddressMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    COKMonitor(GetDataDir() / "blocks" / "addrmon", nCacheSize, fMemory, fWipe)
{
	retryDelay = GetArg("-addrmon_retry_delay", ADDRMON_RETRY_DELAY);
	httpPool = GetArg("-addrmon_http_pool", ADDRMON_HTTP_POOL);
    static boost::asio::io_service::work threadPool(ioService);
}

AddressMonitor::~AddressMonitor()
{

}

bool AddressMonitor::LoadAddresses()
{
    CDBIterator *pcursor = NewIterator();

	CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair('A', uint160());
	pcursor->Seek(ssKeySet.str());

	//Load addresses
	while(pcursor->Valid())
	{
		boost::this_thread::interruption_point();
		try
		{
            std::pair<char, uint160> ssKey;
            if (pcursor->GetKey(ssKey)) {
                if(ssKey.first == 'A')
                {
                    uint160 keyId = ssKey.second;
                    string addressValue;
                    if (pcursor->GetValue(addressValue)) {
                        addressMap.insert(make_pair(keyId, addressValue));
                    }
                }
                else
                {
                    break;
                }
            }
			pcursor->Next();
        } catch (std::exception &e) {
			throw runtime_error(strprintf("%s : Deserialize or I/O error - %s", __func__, e.what()));
		}
	}
	delete pcursor;

	return true;
}

bool AddressMonitor::LoadTransactions()
{
    CDBIterator *pcursor = NewIterator();

	CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair('T', make_pair(int64_t(0), uint256()));
	pcursor->Seek(ssKeySet.str());

	queue<pair<pair<int64_t, uint256>, pair<int, string> > > syncTxQueue;
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
                if(ssKey.first == 'T')
                {
                    int64_t timestamp = ssKey.second.first;
                    uint256 uuid = ssKey.second.second;

                    std::pair<int, string> ssValue;
                    if (pcursor->GetValue(ssValue)) {
                        int type = ssValue.first;
                        std::string json = ssValue.second;

                        if(type == SYNC_TX)
                        {
                            syncTxQueue.push(make_pair(make_pair(timestamp, uuid), make_pair(type, json)));
                        }
                        else if(type == SYNC_CONNECT)
                        {
                            syncConnectQueue.push(make_pair(make_pair(timestamp, uuid), make_pair(type, json)));
                        }
                        else if(type == SYNC_DISCONNECT)
                        {
                            syncDisconnectQueue.push(make_pair(make_pair(timestamp, uuid), make_pair(type, json)));
                        }
//                        else
//                        {
//                            throw runtime_error("unknow type: "+lexical_cast<string>(type));
//                        }
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

	threadGroup.create_thread(boost::bind(&AddressMonitor::LoadSyncTx, this, syncTxQueue));
	threadGroup.create_thread(boost::bind(&AddressMonitor::LoadSyncConnect, this, syncConnectQueue));
	threadGroup.create_thread(boost::bind(&AddressMonitor::LoadSyncDisconnect, this, syncDisconnectQueue));

	return true;
}

void AddressMonitor::LoadSyncTx(queue<pair<pair<int64_t, uint256>, pair<int, string> > > &syncTxQueue)
{
    RenameThread("bitcoin-address-monitor-LoadSyncTx");

    static bool fOneThread;
    if (fOneThread)
    {
        return;
    }
    fOneThread = true;

    MilliSleep(retryDelay * 1000);

    while(!syncTxQueue.empty())
    {
        pair<pair<int64_t, uint256>, pair<int, string> > request = syncTxQueue.front();
        syncTxQueue.pop();
        const string &json = request.second.second;
        const int64_t &now = request.first.first;
        const uint256 &uuid = request.first.second;
        const string requestId = "tx-" + NewRequestId(now, uuid);

        Push_post(requestId, json);
    }
}

void AddressMonitor::LoadSyncConnect(queue<pair<pair<int64_t, uint256>, pair<int, string> > > &syncConnectQueue)
{
    RenameThread("bitcoin-address-monitor-LoadSyncConnect");

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

void AddressMonitor::LoadSyncDisconnect(queue<pair<pair<int64_t, uint256>, pair<int, string> > > &syncDisconnectQueue)
{
    RenameThread("bitcoin-address-monitor-LoadSyncDisconnect");

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


bool AddressMonitor::WriteAddress(const uint160 &keyId, const std::string &address)
{
	return Write(std::make_pair('A', keyId), address);
}

bool AddressMonitor::DeleteAddress(const uint160 &keyId)
{
	return Erase(std::make_pair('A', keyId));
}

bool AddressMonitor::AddAddress(const uint160 &keyId, const std::string &address)
{
    if(!WriteAddress(keyId, address))
    {
    	throw runtime_error("WriteAddress fail: "+address);
    }

    return addressMap.insert(make_pair(keyId, address)).second;
}

bool AddressMonitor::DelAddress(const uint160 &keyId, const std::string &address)
{
	if(!DeleteAddress(keyId))
	{
		throw runtime_error("WriteAddress fail: "+address);
	}

	return addressMap.erase(keyId) == 1;
}

bool AddressMonitor::WriteTx(const int64_t &timestamp, const uint256 &uuid, const int type, const std::string &json)
{
	return Write(std::make_pair('T', std::make_pair(timestamp, uuid)), std::make_pair(type, json), true);
}

bool AddressMonitor::DeleteTx(const int64_t &timestamp, const uint256 &uuid)
{
	return Erase(std::make_pair('T', std::make_pair(timestamp, uuid)), true);
}

bool AddressMonitor::HasAddress(const uint160 &keyId)
{
	return addressMap.find(keyId) != addressMap.end();
}

void AddressMonitor::Start()
{
	if(!LoadAddresses())
	{
		throw runtime_error("AddressMonitor LoadAddresses fail!");
	}

	if(!LoadTransactions())
	{
		throw runtime_error("AddressMonitor LoadTransactions fail!");
	}

	for(int i = 0; i < httpPool; i++)
	{
        threadGroup.create_thread(boost::bind(&io_service_run, this));
	}

    threadGroup.create_thread(boost::bind(&AddressMonitor::PostThread, this));
    threadGroup.create_thread(boost::bind(&AddressMonitor::AckThread, this));
    threadGroup.create_thread(boost::bind(&AddressMonitor::ResendThread, this));
    threadGroup.create_thread(boost::bind(&AddressMonitor::NoResponseCheckThread, this));
}

static UniValue buildValue(const uint256 &txId, const CTransaction &tx, const int n,
		const CBlock *pblock, const string &addressTo,
		const string &blockHash, const int nHeight, const int64_t &time, const int status)
{
    UniValue object(UniValue::VOBJ);
	const CTxOut& txout = tx.vout[n];

	object.push_back(Pair("txid", txId.GetHex()));
	object.push_back(Pair("recTxIndex", n));
	object.push_back(Pair("addressTo", addressTo));
	object.push_back(Pair("amount", (boost::int64_t)(txout.nValue)));
	object.push_back(Pair("time", (boost::int64_t)time));
	object.push_back(Pair("coinType", 1));
	if(pblock)
	{
		object.push_back(Pair("blockHeight", nHeight));
		object.push_back(Pair("blockHash", blockHash));
	}
	object.push_back(Pair("multiFrom", tx.vin.size() > 1 ? 1 : 0));
	object.push_back(Pair("status", status));

	return object;
}

unordered_map<int, uint160> AddressMonitor::GetMonitoredAddresses(const CTransaction &tx,
                                                                  const boost::unordered_map<uint160, std::string> &addresses)
{
	unordered_map<int, uint160> monitorMap;

    for(unsigned long i = 0; i < tx.vout.size(); i++)
	{
		const CTxOut& txout = tx.vout[i];

		CTxDestination dest;
		CBitcoinAddress addr;

        if(!ExtractDestination(txout.scriptPubKey, dest))
		{
			continue;
		}
		else if(addr.Set(dest))
		{
			string address = addr.ToString();
		}

		uint160 keyAddress;
		if(addr.IsScript())
		{
			CScriptID cscriptID = boost::get<CScriptID>(addr.Get());
			keyAddress = cscriptID;
		}
		else
		{
			CKeyID keyID;
			if(!addr.GetKeyID(keyID))
			{
				continue;
			}
			keyAddress = keyID;
		}

        if(addresses.empty() ? HasAddress(keyAddress) : addresses.find(keyAddress) != addresses.end())
		{
			monitorMap.insert(make_pair(i, keyAddress));
		}
	}

	return monitorMap;
}

void AddressMonitor::SyncTransaction(const CTransaction &tx,
                                     const CBlockIndex *pindex,
                                     const CBlock *pblock /*,
                                     const boost::unordered_map<uint160, std::string> &addresses*/)
{
	if(pblock)
	{
		return;
	}

    UniValue ret(UniValue::VARR);
	int64_t now = 0;

	{
		LOCK(cs_address);

		now = GetAdjustedTime();
        const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>();
		const unordered_map<int, uint160> monitorMap = GetMonitoredAddresses(tx, addresses);
		const string blockHash;
		const int nHeight = 0;
		const int status = 0;

		for (unordered_map<int, uint160>::const_iterator it = monitorMap.begin(); it != monitorMap.end(); it++)
		{
			string addressTo = addressMap[it->second];

			ret.push_back(buildValue(tx.GetHash(), tx, it->first, pblock, addressTo,
					blockHash, nHeight, now, status));
		}
	}

	if(ret.size() == 0)
	{
		return;
	}

	uint256 uuid = NewRandomUUID();
	string requestId = "tx-" + NewRequestId(now, uuid);

    UniValue result(UniValue::VOBJ);
	result.push_back(Pair("requestId", requestId));
	result.push_back(Pair("content", ret));
    string json = result.write(0, 0);

	if(!WriteTx(now, uuid, SYNC_TX, json))
	{
		//TODO
	}

    Push_post(requestId, json);
}

void AddressMonitor::SyncConnectBlock(const CBlock *pblock,
                                      CBlockIndex* pindex,
                                      const CTransaction &tx,
                                      const boost::unordered_map<uint160, std::string> &addresses)
{
    UniValue ret(UniValue::VARR);
	int64_t now = 0;

	{
		LOCK(cs_address);

		now = GetAdjustedTime();
		const string blockHash = pblock->GetHash().GetHex();
		const int nHeight = pindex->nHeight;
		const int status = 1;

		unordered_map<int, uint160> monitorMap = GetMonitoredAddresses(tx, addresses);

		for (unordered_map<int, uint160>::const_iterator it = monitorMap.begin(); it != monitorMap.end(); it++)
		{
			string addressTo = addressMap[it->second];

			ret.push_back(buildValue(tx.GetHash(), tx, it->first, pblock, addressTo,
					blockHash, nHeight, now, status));
		}
	}

	if(ret.size() == 0)
	{
		return;
	}

	uint256 uuid = NewRandomUUID();
	string requestId = "conn-" + NewRequestId(now, uuid);

    UniValue result(UniValue::VOBJ);
	result.push_back(Pair("requestId", requestId));
	result.push_back(Pair("content", ret));
    string json = result.write(0, 0);

	if(!WriteTx(now, uuid, SYNC_CONNECT, json))
	{
		//TODO
	}

    Push_post(requestId, json);
}

void AddressMonitor::SyncConnectBlock(const CBlock *pblock,
                                      CBlockIndex* pindex,
                                      const boost::unordered_map<uint160, std::string> &addresses)
{
    UniValue ret(UniValue::VARR);
	int64_t now = 0;

	{
		LOCK(cs_address);

		now = GetAdjustedTime();
		const string blockHash = pblock->GetHash().GetHex();
		const int nHeight = pindex->nHeight;
		const int status = 1;

		BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
		{
			unordered_map<int, uint160> monitorMap = GetMonitoredAddresses(tx, addresses);

			for (unordered_map<int, uint160>::const_iterator it = monitorMap.begin(); it != monitorMap.end(); it++)
			{
				string addressTo = addressMap[it->second];

				ret.push_back(buildValue(tx.GetHash(), tx, it->first, pblock, addressTo,
						blockHash, nHeight, now, status));
			}
		}
	}

	if(ret.size() == 0)
	{
		return;
	}

	uint256 uuid = NewRandomUUID();
	string requestId = "conn-" + NewRequestId(now, uuid);

    UniValue result(UniValue::VOBJ);
	result.push_back(Pair("requestId", requestId));
	result.push_back(Pair("content", ret));
    string json = result.write(0, 0);

	if(!WriteTx(now, uuid, SYNC_CONNECT, json))
	{
		//TODO
	}

    Push_post(requestId, json);
}

void AddressMonitor::SyncDisconnectBlock(const CBlock *pblock)
{
    UniValue ret(UniValue::VARR);
	int64_t now = 0;

	{
		LOCK(cs_address);

		now = GetAdjustedTime();
		string blockHash = pblock->GetHash().GetHex();
		int nHeight = -2;
		const int status = -2;

		BOOST_FOREACH(const CTransaction &tx, pblock->vtx)
		{
			unordered_map<int, uint160> monitorMap = GetMonitoredAddresses(tx);

			for (unordered_map<int, uint160>::const_iterator it = monitorMap.begin(); it != monitorMap.end(); it++)
			{
				string addressTo = addressMap[it->second];

				ret.push_back(buildValue(tx.GetHash(), tx, it->first, pblock, addressTo,
						blockHash, nHeight, now, status));
			}
		}
	}

	if(ret.size() == 0)
	{
		return;
	}

	uint256 uuid = NewRandomUUID();
	string requestId = "dis-" + NewRequestId(now, uuid);

    UniValue result(UniValue::VOBJ);
	result.push_back(Pair("requestId", requestId));
	result.push_back(Pair("content", ret));
    string json = result.write(0, 0);

	if(!WriteTx(now, uuid, SYNC_DISCONNECT, json))
	{
		//TODO
	}

    Push_post(requestId, json);
}

void AddressMonitor::PostActionWrappedException(const std::string &requestId, const std::string& body)
{
	try
	{
        LogAddrmon("PostAction requestId:" + requestId + "\n body:" +body +"\n");
        const string host = GetArg("-addrmon_host", "127.0.0.1");
        const int port = GetArg("-addrmon_port", 80);
        const string url = GetArg("-addrmon_url", "");
        UniValue reply = CallHttpPost(host, port, url, body);
        if (reply.getValStr() == "true" || reply.getValStr() == "false") {
            Push_acked(requestId);
            LogAddrmon("success reply -> requestId: "+requestId+"\n");
        } else {
            LogAddrmon("wrong reply -> requestId: "+requestId+", reply: "+reply.getValStr()+"\n");
        }
	}
	catch(const std::exception& e)
	{
        LogAddrmon("exception in CallHttpPost -> "+string(e.what())+"\n");
	}
	catch(...)
	{
        LogAddrmon("unknow exception in CallHttpPost\n");
	}
}

bool AddressMonitor::AckWrappedExceptioin(const int64_t &timestamp, const uint256 &uuid)
{
    return DeleteTx(timestamp, uuid);
}

void AddressMonitor::PostThread()
{
    RenameThread("bitcoin-address-monitor-post");

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
	    	LogException(&e, string("AddressMonitor::PostThread() -> "+string(e.what())).c_str());
	    	LogAddrmon("AddressMonitor::PostThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogAddrmon("AddressMonitor::PostThread() -> unknow exception\n");
		}
	}
}

void AddressMonitor::AckThread()
{
    RenameThread("bitcoin-address-monitor-ack");

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
				LogAddrmon("do_acked success -> requestId: "+requestId+", json: "+json+"\n");
			else
				LogAddrmon("do_acked fail -> requestId: "+requestId+", json: "+json+"\n");
		}
		catch(std::exception &e)
		{
	    	LogException(&e, string("AddressMonitor::AckThread() -> "+string(e.what())).c_str());
	    	LogAddrmon("AddressMonitor::AckThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogAddrmon("AddressMonitor::AckThread() -> unknow exception\n");
		}
	}
}

void AddressMonitor::ResendThread()
{
    RenameThread("bitcoin-address-monitor-resend");

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
	    	LogException(&e, string("AddressMonitor::ResendThread() -> "+string(e.what())).c_str());
	    	LogAddrmon("AddressMonitor::ResendThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogAddrmon("AddressMonitor::ResendThread() -> unknow exception\n");
		}
	}
}

void AddressMonitor::NoResponseCheckThread()
{
    RenameThread("bitcoin-address-monitor-NoResponseCheck");

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
	    	LogException(&e, string("AddressMonitor::NoResponseCheckThread() -> "+string(e.what())).c_str());
	    	LogAddrmon("AddressMonitor::NoResponseCheckThread() -> "+string(e.what())+"\n");
		}
		catch(...)
		{
			LogAddrmon("AddressMonitor::NoResponseCheckThread() -> unknow exception\n");
		}

		MilliSleep(retryDelay * 1000);
	}
}


