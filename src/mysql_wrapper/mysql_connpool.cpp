/**
 * mysql数据库连接池
 * 2014、04、20
 */

#include <stdexcept>
#include "mysql_connpool.h"
#include "util.h"
            
using namespace std;

static ConnPool * ConnPool::connPool = NULL;

ConnPool::ConnPool(string host,string user,string password,string dbname, int maxSize){
    connectionProperties["hostName"] = host;
    connectionProperties["userName"] = user;
    connectionProperties["password"] = password;
    connectionProperties["OPT_CONNECT_TIMEOUT"] = 600;
    connectionProperties["OPT_RECONNECT"] = true;

    db_name = dbname;
    
    this->maxSize = maxSize;
    this->curSize = 0;
    //初始化driver
    try{
//        this->driver = sql::mysql::get_driver_instance();  //这里不是线程安全的
        this->driver = sql::mysql::get_mysql_driver_instance();
    }
    catch(sql::SQLException &e){
        LogPrintf(" ok_log exception:%s", e.what());
    }
    catch(std::runtime_error &e){
        LogPrintf(" ok_log error:%s", e.what());
    }
    //初始化连接池
    this->Init(maxSize/2);
}

ConnPool::~ConnPool(){    
    this->Destroy();
}

ConnPool *ConnPool::GetInstance(string host,string user,string password,string dbname, int maxSize){
    if(connPool == NULL) {
        connPool = new ConnPool(host,user,password,dbname, maxSize);
    }
    
    return connPool;
}

void ConnPool::Init(int size){
    sql::Connection * conn ;
    pthread_mutex_lock(&lock); 
    
    for(int i = 0; i < size ;){
        conn = this->CreateConnection();
        if(conn){
            i++;            
            conns.push_back(conn);
            ++curSize;
        }
        else{
        }
    }
    
     pthread_mutex_unlock(&lock);    
}

void ConnPool::TerminateConnection(sql::Connection * conn){
    if(conn){
        try{
            conn->close();
        }
        catch(sql::SQLException &e){
            LogPrintf(" ok_log exception:%s", e.what());
        }
        catch(std::runtime_error &e){
            LogPrintf(" ok_log error:%s", e.what());
        }

        delete conn;
    }
}

void ConnPool::Destroy(){
    list<sql::Connection *>::iterator pos;
    
      pthread_mutex_lock(&lock);  
    
    for(pos = conns.begin(); pos != conns.end();++pos){
        this->TerminateConnection(*pos);
    }
    
    curSize = 0;
    conns.clear();
    pthread_mutex_unlock(&lock);    
}

sql::Connection * ConnPool::CreateConnection(){//这里不负责curSize的增加
    sql::Connection *conn;
    
    try{
        conn = driver->connect(connectionProperties);
        conn->setSchema(db_name);
        return conn;
    }
    catch(sql::SQLException &e){
        return NULL;
    }
    catch(std::runtime_error &e){
        return NULL;
    }
}


sql::Connection * ConnPool::GetConnection(){
    sql::Connection * conn = NULL;
    
    pthread_mutex_lock(&lock);
    if(conns.size() > 0){//有空闲连接,则返回
        conn = conns.front();
        conns.pop_front();
        
        if(conn->isClosed()){ //如果连接关闭,则重新打开一个连接
            delete conn;
            conn = this->CreateConnection();
        }
        
        if(conn == NULL){ //创建连接不成功
            --curSize;
        }

    }
    else{
        if(curSize < maxSize){//还可以创建新的连接
            conn = this->CreateConnection();
            if(conn){
                ++curSize;
            }
        }
        else{//连接池已经满了
          conn = NULL;
        }
    }    

    pthread_mutex_unlock(&lock);
    return conn;
}

void ConnPool::ReleaseConnection(sql::Connection * conn){
    if(conn){
        pthread_mutex_lock(&lock);  
        
        conns.push_back(conn);    
        pthread_mutex_unlock(&lock);
    }
}

sql::Connection * ConnPool::GetConnectionTry(int maxNum){
    sql::Connection * conn;
    
    for(int i = 0; i < maxNum; ++i){
        conn = this->GetConnection();
        if(conn){
            return conn;
        }
        else {
            sleep(50);
        }
    }
    
    return NULL;
}
