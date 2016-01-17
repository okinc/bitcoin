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
#define DB_USER	 		"coinuser"
#define DB_PASSWORD		"123456"
#define DB_NAME			"coinokdata"
#define MAX_CONNCOUNT	50


using namespace sql;
using namespace sql::mysql;

static std::string db_server;
static std::string db_user;
static std::string db_password;
static std::string db_name;
static ConnPool *pConnPool;



#else

#endif

static bool fInited = false;


bool OKCoin_Log_init(){
    LogPrintf("ok-- OKCoin_Log_init flag 1\n");
	if(fInited == true){
        LogPrintf("ok-- OKCoin_Log_init allready inited\n");
		return false;
	}
#if LOG2DB
	/* Create a connection */
	//load config
    LogPrintf("OKCoin_Log_init get config args\n");
	db_server= GetArg("-okdbhost", DB_SERVER);
	db_user = GetArg("-okdbuser", DB_USER);
	db_password = GetArg("-okdbpassword", DB_PASSWORD);
	db_name= GetArg("-okdbname", DB_NAME);
	
    LogPrintf( "OKCoin_Log_init loadconfig ok_db_host = %s\n", db_server);

    pConnPool = ConnPool::GetInstance(db_server,db_user,db_password,db_name,MAX_CONNCOUNT);
  	fInited = pConnPool ? true: false;
	

	
#else
    fInited = true;
#endif
    LogPrintf("ok-- OKCoin_Log_init result = %d\n", fInited);
    return fInited;
}


bool OKCoin_Log_deInit(){
#if LOG2DB

	if(pConnPool){
		delete pConnPool;
		pConnPool = NULL;
	}
#else

#endif

	fInited = false;
    LogPrintf("ok-- OKCoin_Log_deInit\n");
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
    ret = 0;
#endif
    LogPrintf("ok-- Log_Event Event(type=%d, action= %d, hash=%s, from=%s),result(%d)\n", type, action,hash,fromip,ret);
	return ret;
}


int OKCoin_Log_Event(const COKLogEvent &event){
    if(!event.IsNull()){
       return OKCoin_Log_Event(event.mType, event.mAction, event.mHashCode, event.mFromIP);
    }
    return 0;
}




int OKCoin_Log_EarseOrphaneBlk(std::string blkHash){
   return OKCoin_Log_Event(OC_TYPE_BLOCK, OC_ACTION_ORPHANE,  blkHash, "127.0.0.1");
}

int OKCoin_Log_EarseOrphaneTx(std::string txHash){
   return OKCoin_Log_Event(OC_TYPE_TX, OC_ACTION_ORPHANE,  txHash, "127.0.0.1");
}

std::string COKLogEvent::ToString() const{
    return strprintf(
        "COKLogEvent("
        "    type    = %d"
        "    action  = %d"
        "    hash    = %s"
        "    ip      = %s"
        ")\n",
        mType,
        mAction,
        mHashCode,
        mFromIP );
}

