//Copyright (c) 2014-2016 OKCoin
//Author : Chenzs
//2014/04/06

#ifndef OKCOIN_LOG_H
#define OKCOIN_LOG_H


#define OKCOIN_LOG
#define _MYSQL_DB_   //写到SQL数据库

#ifdef _MYSQL_DB_
#define LOG2DB 			1
#else
#define LOG2DB 			0 	//写到文件
#endif 


#include <stdint.h>
#include <string>
#include "uint256.h"
#include "serialize.h"

#if LOG2DB

#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>

	
#else

/*
 Log to file
 */
#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp> 
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>
#define strprintf tfm::format
#define OKCoinLogPrintf(...) OKCoinLogPrint(__VA_ARGS__)


int OKCoinLogPrintStr(const std::string &str);

#define MAKE_OKCOIN_LOG_FUNC(n)                                        \
    template<TINYFORMAT_ARGTYPES(n)>                                          \
    static inline int OKCoinLogPrint(const char* format, TINYFORMAT_VARARGS(n))  \
    {                                                                         \
        return OKCoinLogPrintStr(tfm::format(format, TINYFORMAT_PASSARGS(n))); \
    }                                                                         \
    
TINYFORMAT_FOREACH_ARGNUM(MAKE_OKCOIN_LOG_FUNC)
static inline int OKCoinLogPrint(const char* format)
{
    return LogPrintStr(format);
}

#endif

class COKLogEvent;


enum OKCoin_EventType{
    OC_TYPE_BLOCK = 0,//block
    OC_TYPE_TX = 1  //transaction
 } ;

enum OKCoin_Action {
    OC_ACTION_NEW = 0,  //tx, block产生
    OC_ACTION_CONFIRM = 1,//tx 确认
    OC_ACTION_ORPHANE = -1,//tx,block 孤立
 } ;

extern bool OKCoin_Log_init();
extern bool OKCoin_Log_deInit();

/**
* 2014/07/11 chenzs
* type -- block:0 tx:1  
*/
int OKCoin_Log_Event(const COKLogEvent &event);
int OKCoin_Log_Event(const int& type, int& action , const std::string& hash, const std::string& fromip="127.0.0.1");
//剔除孤立数据
int OKCoin_Log_EarseOrphaneBlk(std::string blkHash);
int OKCoin_Log_EarseOrphaneTx(std::string txHash);

class COKLogEvent{
public:
    int mType;
    int mAction;
    std::string mHashCode;
    std::string mFromIP;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mType);
        READWRITE(mAction);
        READWRITE(mHashCode);
        READWRITE(mFromIP);
    }

    void SetNull(){
        mType = -1;
        mAction = -1;
        mHashCode.clear();
        mFromIP.clear();
    }

    bool IsNull() const{
        return ((mType == -1) || mHashCode.empty());
    }

    COKLogEvent(){
        SetNull();
    }

    COKLogEvent(const int& type, const int& action , const std::string& hash, const std::string& fromip="127.0.0.1"):mType(type),
        mAction(action), mHashCode(hash), mFromIP(fromip){}

    COKLogEvent(const COKLogEvent& other){
        mType = other.mType;
        mAction = other.mAction;
        mHashCode = other.mHashCode;
        mFromIP = other.mFromIP;
    }

    std::string ToString() const;
};


#endif
