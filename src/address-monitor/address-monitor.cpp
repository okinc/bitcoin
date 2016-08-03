#include "address-monitor/address-monitor.h"
#include "serialize.h"
#include "util.h"
#include "timedata.h"
#include "base58.h"
#include "script/script.h"
#include "main.h"

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

#include "univalue.h"

//#include "json/json_spirit_value.h"
//#include "json/json_spirit_reader_template.h"
//#include "json/json_spirit_writer_template.h"
//#include "json/json_spirit_utils.h"

using namespace std;
using namespace boost;
using namespace boost::asio;
using boost::lexical_cast;
using boost::unordered_map;

static boost::asio::io_service ioService;
static boost::asio::io_service::work threadPool(ioService);

static void io_service_run(void)
{
	ioService.run();
}


AddressMonitor::AddressMonitor(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "blocks" / "addrmon", nCacheSize, fMemory, fWipe),
	retryDelay(0), httpPool(ADDRMON_HTTP_POOL), sem_post(0), sem_acked(0), sem_resend(0), is_stop(false)
{
	retryDelay = GetArg("-addrmon_retry_delay", ADDRMON_RETRY_DELAY);
	httpPool = GetArg("-addrmon_http_pool", ADDRMON_HTTP_POOL);
}

bool AddressMonitor::LoadAddresses()
{
//    leveldb::Iterator *pcursor = NewIterator();
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
            leveldb::Slice slKey;
            if (pcursor->GetKey(slKey)) {
                CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
                char chType;
                ssKey >> chType;
                if(chType == 'A')
                {
                    leveldb::Slice slValue;
                    if (pcursor->GetValue(slValue)) {
                        CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);

                        uint160 keyId;
                        ssKey >> keyId;

                        string address;
                        ssValue >> address;

                        addressMap.insert(make_pair(keyId, address));
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
            leveldb::Slice slKey;
            if (pcursor->GetKey(slKey)) {
                CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
                char chType;
                ssKey >> chType;
                if(chType == 'T')
                {
                    leveldb::Slice slValue;
                    if (pcursor->GetValue(slValue)) {
                        CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);

                        int64_t timestamp;
                        ssKey >> timestamp;

                        uint256 uuid;
                        ssKey >> uuid;

                        int type;
                        ssValue >> type;

                        string json;
                        ssValue >> json;

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
                        else
                        {
                            throw runtime_error("unknow type: "+lexical_cast<string>(type));
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
			throw runtime_error(strprintf("%s : Deserialize or I/O error - %s", __func__, e.what()));
		}
	}
	delete pcursor;

	threadGroup.create_thread(boost::bind(&AddressMonitor::LoadSyncTx, this, syncTxQueue));
	threadGroup.create_thread(boost::bind(&AddressMonitor::LoadSyncConnect, this, syncConnectQueue));
	threadGroup.create_thread(boost::bind(&AddressMonitor::LoadSyncDisconnect, this, syncDisconnectQueue));

	return true;
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

void AddressMonitor::Load()
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
		threadGroup.create_thread(boost::bind(&io_service_run));
	}

    threadGroup.create_thread(boost::bind(&AddressMonitor::PostThread, this));
    threadGroup.create_thread(boost::bind(&AddressMonitor::AckThread, this));
    threadGroup.create_thread(boost::bind(&AddressMonitor::ResendThread, this));
    threadGroup.create_thread(boost::bind(&AddressMonitor::NoResponseCheckThread, this));
}

UniValue buildValue(const uint256 &txId, const CTransaction &tx, const int n,
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

	push_post(requestId, json);
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

	push_post(requestId, json);
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

	push_post(requestId, json);
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

	push_post(requestId, json);
}

bool AddressMonitor::ack(const string &requestId)
{
	push_acked(requestId);
	return true;
}

const uint256 AddressMonitor::NewRandomUUID() const
{
    uint256 uuid = GetRandHash();
	return uuid;
}

const std::string AddressMonitor::NewRequestId(const int64_t &now, const uint256 &uuid) const
{
	CDataStream ssId(SER_DISK, CLIENT_VERSION);
	ssId << now;
	ssId << uuid;

	return EncodeBase58((const unsigned char*)&ssId[0], (const unsigned char*)&ssId[0] + (int)ssId.size());
}

bool AddressMonitor::decodeRequestIdWithoutPrefix(const std::string &requestIdWithoutPrefix, int64_t &now, uint256 &uuid)
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

bool AddressMonitor::decodeRequestIdWitPrefix(const std::string &requestIdWithPrefix, int64_t &now, uint256 &uuid)
{
	if(requestIdWithPrefix.substr(0, 3) == "tx-")
	{
		return decodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(3), now, uuid);
	}
	else if(requestIdWithPrefix.substr(0, 5) == "conn-")
	{
		return decodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(5), now, uuid);
	}
	else if(requestIdWithPrefix.substr(0, 5) == "dis-")
	{
		return decodeRequestIdWithoutPrefix(requestIdWithPrefix.substr(4), now, uuid);
	}
	else
	{
		return false;
	}
}

const std::string AddressMonitor::NewRequestId() const
{
	const int64_t now = GetAdjustedTime();
	const uint256 uuid = NewRandomUUID();

	return NewRequestId(now, uuid);
}

void AddressMonitor::push_post(const std::string &requestId, const std::string &json)
{
	LOCK2(cs_map, cs_post);

	requestMap.insert(make_pair(requestId, json));
	postQueue.push(requestId);

	sem_post.post();
}

void AddressMonitor::push_acked(const std::string &requestId)
{
	LOCK(cs_acked);

	ackedQueue.push(requestId);

	sem_acked.post();
}

void AddressMonitor::push_resend(const std::string &requestId)
{
	LOCK(cs_resend);

	resendQueue.push(make_pair(requestId, GetAdjustedTime() + retryDelay));

	sem_resend.post();
}

bool AddressMonitor::pull_post(std::string &requestId, const std::string ** const ppjson)
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

bool AddressMonitor::pull_acked(std::string &requestId, const std::string ** const ppjson)
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

bool AddressMonitor::pull_resend(std::string &requestId, const std::string ** const ppjson)
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


static void CallRPC(AddressMonitor* self, const std::string &requestId, const string& body)
{
    // Connect to localhost
    bool fUseSSL = false;
    asio::io_service io_service;
    ssl::context context(io_service, ssl::context::sslv23);
    context.set_options(ssl::context::no_sslv2 | ssl::context::no_sslv3);
    asio::ssl::stream<asio::ip::tcp::socket> sslStream(io_service, context);
    SSLIOStreamDevice<asio::ip::tcp> d(sslStream, fUseSSL);
    iostreams::stream< SSLIOStreamDevice<asio::ip::tcp> > stream(d);

    const bool fWait = false;
    const string host = GetArg("-addrmon_host", "127.0.0.1");
    const string port = GetArg("-addrmon_port", "80");
    const string url = GetArg("-addrmon_url", "");

    const bool fConnected = d.connect(host, port);
    if (!fConnected)
    {
    	LogAddrmon("couldn't connect to server");
    	return;
    }

    map<string, string> mapRequestHeaders;

    // Send request
    string strPost = HTTPPost(body, mapRequestHeaders, host, url);
    stream << strPost << std::flush;

    // Receive HTTP reply status
    int nProto = 0;
    int nStatus = ReadHTTPStatus(stream, nProto);

    // Receive HTTP reply message headers and body
    map<string, string> mapHeaders;
    string strReply;
    ReadHTTPMessage(stream, mapHeaders, strReply, nProto, std::numeric_limits<size_t>::max());

    if (nStatus == HTTP_UNAUTHORIZED)
    	LogAddrmon("incorrect rpcuser or rpcpassword (authorization failed)");
    else if (nStatus >= 400 && nStatus != HTTP_BAD_REQUEST && nStatus != HTTP_NOT_FOUND && nStatus != HTTP_INTERNAL_SERVER_ERROR)
    	LogAddrmon(strprintf("server returned HTTP error %d", nStatus));
    else if (strReply.empty())
    	LogAddrmon("no response from server");
    else if(strReply == "true")
    {
    	self->ack(requestId);
        LogAddrmon("success reply -> requestId: "+requestId+"\n");
    }
    else
    {
        LogAddrmon("wrong reply -> requestId: "+requestId+", reply: "+strReply+"\n");
    }
}

static void CallRPCWrappedException(AddressMonitor* self, const std::string &requestId, const string& body)
{
	try
	{
		CallRPC(self, requestId, body);
	}
	catch(const std::exception& e)
	{
		LogAddrmon("exception in CallRPC -> "+string(e.what())+"\n");
	}
	catch(...)
	{
		LogAddrmon("unknow exception in CallRPC\n");
	}
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

		if(!pull_post(requestId, &pjson))
		{
			MilliSleep(1000);
			continue;
		}

		try
		{
			do_post(requestId, pjson);
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

		if(!pull_acked(requestId, &pjson))
		{
			MilliSleep(1000);
			continue;
		}
		json = *pjson;

		try
		{
			if(do_acked(requestId))
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

		if(!pull_resend(requestId, &pjson))
		{
			MilliSleep(1000);
			continue;
		}

		try
		{
			do_resend(requestId, pjson);
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

bool AddressMonitor::do_post(const std::string &requestId, const std::string * pjson)
{
	LogAddrmon("do_post -> requestId: "+requestId+", json: "+*pjson+"\n");
	ioService.post(boost::bind(CallRPCWrappedException, this, requestId, *pjson));
	return true;
}

bool AddressMonitor::do_acked(const std::string &requestId)
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

bool AddressMonitor::do_resend(const std::string &requestId, const std::string * pjson)
{
	LogAddrmon("do_resend -> requestId: "+requestId+", json: "+*pjson+"\n");
	ioService.post(boost::bind(CallRPCWrappedException, this, requestId, *pjson));
	return true;
}

void AddressMonitor::NoResponseCheck()
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

		push_post(requestId, json);
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

		push_post(requestId, json);
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

		push_post(requestId, json);
    }
}

void AddressMonitor::Stop()
{
	is_stop = true;
	sem_post.post();
	sem_acked.post();
	sem_resend.post();

	ioService.stop();
	threadGroup.interrupt_all();
	threadGroup.join_all();
}

