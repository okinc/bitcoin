/*
 * address-monitor.h
 *
 *  Created on: 2014年10月28日
 *      Author: Administrator
 */

#ifndef ADDRESS_MONITOR_H_
#define ADDRESS_MONITOR_H_

#include "cokmonitor.h"

#define ADDRMON_RETRY_DELAY	600
#define ADDRMON_HTTP_POOL	10




class AddressMonitor :public COKMonitor
{
public:
	AddressMonitor(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    virtual ~AddressMonitor();
private:

    void LoadSyncTx(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncTxQueue);
    void LoadSyncConnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncConnectQueue);
    void LoadSyncDisconnect(std::queue<std::pair<std::pair<int64_t, uint256>, std::pair<int, std::string> > > &syncDisconnectQueue);

public:
    mutable CCriticalSection cs_address;

    virtual void Start();

    bool AddAddress(const uint160 &keyId, const std::string &address);
    bool DelAddress(const uint160 &keyId, const std::string &address);
    bool HasAddress(const uint160 &keyId);


    void SyncTransaction(const CTransaction &tx,
                         const CBlockIndex *pindex,
                         const CBlock *pblock);

    /*
    void SyncTransaction(const CTransaction &tx,
                         const CBlock *pblock,
                         const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());
    */

    void SyncConnectBlock(const CBlock *pblock,
                          CBlockIndex* pindex,
                          const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());

    void SyncDisconnectBlock(const CBlock *pblock);

    //RPC:resynctx同步tx使用
    void SyncConnectBlock(const CBlock *pblock,
                          CBlockIndex* pindex,
                          const CTransaction &tx,
                          const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());

    virtual void PostActionWrappedException(const std::string &requestId, const std::string& body);
    virtual bool AckWrappedExceptioin(const int64_t &timestamp, const uint256 &uuid);

protected:
    boost::unordered_map<int, uint160> GetMonitoredAddresses(const CTransaction &tx, const boost::unordered_map<uint160, std::string> &addresses=boost::unordered_map<uint160, std::string>());

private:
    enum
    {
        SYNC_TX = 1,
        SYNC_CONNECT = 2,
        SYNC_DISCONNECT = 3
    };

    void PostThread();
    void AckThread();
    void ResendThread();
    void NoResponseCheckThread();

//    void NoResponseCheck();

private:

    bool LoadAddresses();
    bool LoadTransactions();

    bool WriteAddress(const uint160 &keyId, const std::string &address);
    bool DeleteAddress(const uint160 &keyId);

    bool WriteTx(const int64_t &timestamp, const uint256 &uuid, const int type, const std::string &json);
    bool DeleteTx(const int64_t &timestamp, const uint256 &uuid);

    boost::unordered_map<uint160, std::string> addressMap;
    boost::unordered_map<std::pair<uint256, uint160>, std::pair<int, bool> > txMap;


};

#endif /* ADDRESS_MONITOR_H_ */
