//Copyright (c) 2014-2016 OKCoin
//Author : Chenzs
//2014/04/06

#include "okcoin_log.h"
#include "util.h"
#include "timedata.h"

//使用连接池方式
#include "mysql_connpool.h"


#if LOG2DB
#define DB_SERVER 		"127.0.0.1:3306"
#define DB_USER	 		"root"
#define DB_PASSWORD		"root"
#define DB_NAME			"blockchain"
#define MAX_CONNCOUNT	50


using namespace sql;
using namespace sql::mysql;

static std::string db_server;
static std::string db_user;
static std::string db_password;
static std::string db_name;
static ConnPool *pConnPool;



#else
//static boost::once_flag debugPrintInitFlag = BOOST_ONCE_INIT;
static FILE* okcoinFileout = NULL;
static boost::mutex* mutexOkcoinLog = NULL;
#define  OKCOIN_LOG_FILENAME		"okcoin_tx.log"
#endif

static bool fInited = false;


bool OKCoin_Log_init(){
	if(fInited == true){
		LogPrint("okcoin_log", "okcoin_log allready inited\n");
		return false;
	}
#if LOG2DB
	/* Create a connection */
	//load config
	db_server= GetArg("-okdbhost", DB_SERVER);
	db_user = GetArg("-okdbuser", DB_USER);
	db_password = GetArg("-okdbpassword", DB_PASSWORD);
	db_name= GetArg("-okdbname", DB_NAME);
	
	LogPrint("okcoin_log", "OKCoin_Log_init loadconfig ok_db_host = %s\n", db_server);

    pConnPool = ConnPool::GetInstance(db_server,db_user,db_password,db_name,MAX_CONNCOUNT);
  	fInited = pConnPool ? true: false;
	

	
#else
	assert(okcoinFileout == NULL);
    assert(mutexOkcoinLog == NULL);

    boost::filesystem::path pathDebug = GetDataDir() / OKCOIN_LOG_FILENAME;
    okcoinFileout = fopen(pathDebug.string().c_str(), "a");
    if (okcoinFileout) {
    	setbuf(okcoinFileout, NULL); // unbuffered
    	fInited = true;
    }
    mutexOkcoinLog = new boost::mutex();
#endif
    return fInited;
}


bool OKCoin_Log_deInit(){
#if LOG2DB

	if(pConnPool){
		delete pConnPool;
		pConnPool = NULL;
	}
#else
	if(okcoinFileout != NULL)
	{
		fclose(okcoinFileout);
		okcoinFileout = NULL;
	}
	
	if(mutexOkcoinLog != NULL){
		delete mutexOkcoinLog;
		mutexOkcoinLog = NULL;
	}
#endif

	fInited = false;
	LogPrint("okcoin_log", "OKCoin_Log_deInit\n");
	return true;
}



/**
* type -- block:0 tx:1  
*/
int OKCoin_Log_Event(const int& type, const int& action, const std::string& hash, const std::string& fromip){
	assert(fInited == true);
    int ret = -1;
#if LOG2DB
	/*
	if(pstmtEvent == NULL){
		pstmtEvent = mysqlConn->prepareStatement("CALL InsertEvent(?,?,?,?,?,?)");
	}
	*/
	sql::Connection *pConn = pConnPool->GetConnection();
	assert(pConn != NULL);
	std::auto_ptr<PreparedStatement> pstmtEvent(pConn->prepareStatement("CALL InsertEvent(?,?,?,?,?,?)"));
	try{
		pstmtEvent->setInt(1, type);
		pstmtEvent->setInt(2, action);
		pstmtEvent->setString(3, hash);
		pstmtEvent->setString(4, fromip);
		pstmtEvent->setInt(5, 0);
        pstmtEvent->setDateTime(6,DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetAdjustedTime()));
		ret = pstmtEvent->executeUpdate();
		pstmtEvent->close();
	}catch(sql::SQLException &e){
		LogPrint("okcoin_log", "okcoin_log Insert Event type=%d err %s \n", type, e.what());
        ret = -2;
	}
	pConnPool->ReleaseConnection(pConn);

#else
	ret = OKCoinLogPrint("action:%d, type:%d block:%s ip:%s rt:%lu\n", action, type, hash.data(), fromip.data(), GetTime());
#endif
	LogPrint("okcoin_log", "okcoin_log Insert Event type=%d result= %s \n", type, ret);
	return ret;
}


int OKCoin_Log_Event(const COKLogEvent &event){
    if(!event.IsNull()){
       return OKCoin_Log_Event(event.mType, event.mAction, event.mHashCode, event.mFromIP);
    }
    return 0;
}


#if !LOG2DB
int OKCoinLogPrintStr(const std::string &str)
{
	int ret = 0; // Returns total number of characters written
	
    if (okcoinFileout == NULL)
        return ret;

    boost::mutex::scoped_lock scoped_lock(*mutexOkcoinLog);
    ret += fprintf(okcoinFileout, "%s ", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
    ret = fwrite(str.data(), 1, str.size(), okcoinFileout);
    return ret;
}
#endif

int OKCoin_Log_EarseOrphaneBlk(std::string blkHash){
   return OKCoin_Log_Event(OC_TYPE_BLOCK, OC_ACTION_ORPHANE,  blkHash, "127.0.0.1");
}

int OKCoin_Log_EarseOrphaneTx(std::string txHash){
   return OKCoin_Log_Event(OC_TYPE_TX, OC_ACTION_ORPHANE,  txHash, "127.0.0.1");
}

std::string COKLogEvent::ToString() const{
    return strprintf(
        "COKLogEvent(\n"
        "    type    = %d\n"
        "    action  = %d\n"
        "    hash    = %s\n"
        "    ip      = %s\n"
        ")\n",
        mType,
        mAction,
        mHashCode,
        mFromIP );
}

