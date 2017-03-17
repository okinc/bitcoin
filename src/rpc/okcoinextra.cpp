/*
 * okcoinextra.cpp
 *
 *  Created on: 2016年08月02日
 *      Author: chenzs
 */
#include "rpc/server.h"
#include "init.h"
#include "main.h"
#include "chainparams.h"
#include "clientversion.h"
#include "key.h"
#include "base58.h"
#include "protocol.h"
#include "sync.h"
#include "timedata.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "version.h"
#include "validationinterface.h"
#include "address-monitor/address-monitor.h"
#include "address-monitor/block-monitor.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
#include <boost/foreach.hpp>

#include <univalue.h>

using namespace std;
using std::runtime_error;

/**
 * @brief add a bitcoinaddress to monitor
 * @param bitcoinaddress
 * @param fHelp
 * @return
 */
UniValue addmonitor(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error(
            "addmonitor \"bitcoinaddress\"\n"
            "\nadd a bitcoinaddress to monitor.\n"
            "\nResult\n"
            "\"bool\"      (string) true if not exsit; false if already exsit\n"
            "\nExamples\n"
        );

    UniValue ret(UniValue::VARR);

    for(unsigned long i = 0; i < params.size(); i++)
    {
        UniValue array(UniValue::VARR);
        UniValue param = params[i];
        if(param.isStr() && !CBitcoinAddress().SetString(param.get_str()))
        {
            UniValue value(UniValue::VARR);
            value.read(param.get_str());
            array = value.get_array();
        }
        else if(param.isArray())
        {
            array.push_back(param.get_array());
        }
        else
        {
            array.push_back(param);
        }

//        BOOST_FOREACH(UniValue value, array)
        for(unsigned long j = 0; j < array.size(); j++)
        {

            string strAddress = array[j].get_str();
            CBitcoinAddress address;
            if (!address.SetString(strAddress))
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");

            uint160 addressKey;
            if(!address.IsScript())
            {
                CKeyID keyID;
                if (!address.GetKeyID(keyID))
                    throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
                addressKey = keyID;
            }
            else
            {
                CScriptID cscriptID = boost::get<CScriptID>(address.Get());
                addressKey = cscriptID;
            }

            LOCK(paddressMonitor->cs_address);

            bool insertNew;
            if(paddressMonitor->HasAddress(addressKey))
            {
                insertNew = false;
            }
            else
            {
                insertNew = paddressMonitor->AddAddress(addressKey, strAddress);
            }

            UniValue object(UniValue::VOBJ);
            object.push_back(Pair("address", strAddress));
            object.push_back(Pair("ret", insertNew));
            ret.push_back(object);
        }
    }

    LOCK(paddressMonitor->cs_address);
    if(!paddressMonitor->Sync())
    {
        throw runtime_error("Sync addmonitor fail!");
    }

    return ret;
}


UniValue delmonitor(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error(
            "delmonitor \"bitcoinaddress\"\n"
            "\ndelete a bitcoinaddress monitored.\n"
            "\nResult\n"
            "\"bool\"      (string) true if exsit; false if not exsit\n"
            "\nExamples\n"
        );

    UniValue ret(UniValue::VARR);

    for(unsigned long i = 0; i < params.size(); i++)
    {
        UniValue array(UniValue::VARR);
        UniValue param = params[i];
        if(param.isStr() && !CBitcoinAddress().SetString(param.get_str()))
        {
            UniValue value(UniValue::VARR);
            value.read(param.get_str());
            array.push_back(value.get_array());
        }
        else if(param.isArray())
        {
             array.push_back(param.get_array());
        }
        else
        {
            array.push_back(param);
        }

        for(unsigned long j = 0; j < array.size(); j++)
        {
            string strAddress = array[j].get_str();
            CBitcoinAddress address;
            if (!address.SetString(strAddress))
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");

            uint160 addressKey;
            if(!address.IsScript())
            {
                CKeyID keyID;
                if (!address.GetKeyID(keyID))
                    throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
                addressKey = keyID;
            }
            else
            {
                CScriptID cscriptID = boost::get<CScriptID>(address.Get());
                addressKey = cscriptID;
            }

            LOCK(paddressMonitor->cs_address);
            bool alreadyHas;
            if(!paddressMonitor->HasAddress(addressKey))
            {
                alreadyHas = false;
            }
            else
            {
                alreadyHas = paddressMonitor->DelAddress(addressKey, strAddress);
            }

            UniValue object(UniValue::VOBJ);
            object.push_back(Pair("address", strAddress));
            object.push_back(Pair("ret", alreadyHas));
            ret.push_back(object);
        }
    }

    LOCK(paddressMonitor->cs_address);
    if(!paddressMonitor->Sync())
    {
        throw runtime_error("Sync delmonitor fail!");
    }

    return ret;
}

UniValue ismonitor(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() == 0)
        throw runtime_error(
            "ismonitor \"bitcoinaddress\"\n"
            "\test a bitcoinaddress is monitored.\n"
            "\nResult\n"
            "\"bool\"      (string) true if monitored; false if not\n"
            "\nExamples\n"
        );

    UniValue ret(UniValue::VARR);

    for(unsigned long i = 0; i < params.size(); i++)
    {
        UniValue array(UniValue::VARR);
        UniValue param = params[i];
        if(param.isStr() && !CBitcoinAddress().SetString(param.get_str()))
        {
            UniValue value(UniValue::VARR);
            value.read(param.get_str());
            array = value.get_array();
        }
        else if(param.isArray())
        {
           array = param.get_array();
        }
        else
        {
            array.push_back(param);
        }

        for(unsigned long j = 0; j < array.size(); j++)
        {
            string strAddress = array[j].get_str();
            CBitcoinAddress address;
            if (!address.SetString(strAddress))
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");

            uint160 addressKey;
            if(!address.IsScript())
            {
                CKeyID keyID;
                if (!address.GetKeyID(keyID))
                    throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
                addressKey = keyID;
            }
            else
            {
                CScriptID cscriptID = boost::get<CScriptID>(address.Get());
                addressKey = cscriptID;
            }

            LOCK(paddressMonitor->cs_address);

            UniValue object(UniValue::VOBJ);
            object.push_back(Pair("address", strAddress));
            object.push_back(Pair("ret", paddressMonitor->HasAddress(addressKey)));
            ret.push_back(object);
        }
    }

    return ret;
}

UniValue ackmonitor(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "ackmonitor \"requestId\"\n"
            "\ack a monitor request.\n"
            "\nResult\n"
            "\"bool\"      (string) true if success\n"
            "\nExamples\n"
        );


    UniValue param = params[0];
    const string requestId = param.get_str();

    return paddressMonitor->Ack(requestId);
}


UniValue ackblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "ackblock \"requestId\"\n"
            "\ack a monitor request.\n"
            "\nResult\n"
            "\"bool\"      (string) true if success\n"
            "\nExamples\n"
        );


    UniValue param = params[0];
    const string requestId = param.get_str();

    return pblockMonitor->Ack(requestId);
}


UniValue resynctx(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "resynctx \"txId\"\n"
            "\re-sync a tx.\n"
            "\nResult\n"
            "\"bool\"      (string) true if confirms > 0\n"
            "\nExamples\n"
            + HelpExampleCli("resynctx", "\"txId\"")
        );

    uint256 txId = ParseHashV(params[0], "parameter 1");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txId, tx, Params().GetConsensus(),hashBlock, true))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");
    }

    const bool confirmed = !hashBlock.IsNull();

    LOCK(paddressMonitor->cs_address);

    paddressMonitor->SyncTransaction(tx, NULL, NULL);
    if(confirmed)
    {
        if (mapBlockIndex.count(hashBlock) == 0)
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }

        CBlock block;
        CBlockIndex* pblockindex = mapBlockIndex[hashBlock];

        if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }

        paddressMonitor->SyncConnectBlock(&block, pblockindex, tx);
    }

    return confirmed;
}

UniValue rescan(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
            "rescan \"block\" \"addresses\"\n"
            "\rescan from block.\n"
            "\nResult\n"
            "\"bool\"      (string) true if success\n"
            "\nExamples\n"
            + HelpExampleCli("rescan", "\"blockhash\"")
        );

    //全量重扫 2017、03、17
/*
    boost::unordered_map<uint160, std::string> addresses;

    if(params.size() > 1)
    {
       for(unsigned long i = 1; i < params.size(); i++)
       {
            UniValue array(UniValue::VARR);
            UniValue param = params[i];
            if(param.isStr() && !CBitcoinAddress().SetString(param.get_str()))
            {
                UniValue value;
                value.read(param.get_str());
                array = value.get_array();
            }
            else if(param.isArray())
            {
                array.push_back(param.get_array());
            }
            else
            {
                array.push_back(param);
            }

            for(unsigned long j = 0; j < array.size(); j++)
            {
                string strAddress = array[j].get_str();
                CBitcoinAddress address;
                if (!address.SetString(strAddress))
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address: "+strAddress);

                uint160 addressKey;
                if(!address.IsScript())
                {
                    CKeyID keyID;
                    if (!address.GetKeyID(keyID))
                        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
                    addressKey = keyID;
                }
                else
                {
                    CScriptID cscriptID = boost::get<CScriptID>(address.Get());
                    addressKey = cscriptID;
                }

                addresses[addressKey] = strAddress;
            }
        }
    }
*/
#ifdef ENABLE_WALLET
    {
        LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);

        CBlockIndex *pindexRescan;

        if(params.size() > 0)
        {
            uint256 blockHash = ParseHashV(params[0], "parameter 1");

            vector<uint256> vHaveIn;
            vHaveIn.push_back(blockHash);
            CBlockLocator locator(vHaveIn);
            pindexRescan = FindForkInGlobalIndex(chainActive, locator);
            if(pindexRescan == NULL)
            {
                throw runtime_error("can not find block: "+blockHash.ToString());
            }
        }
        else
        {
            pindexRescan = chainActive.Genesis();
        }
        pwalletMain->ScanForWalletTransactions(pindexRescan, true /*, addresses*/);
    }
#endif

    return true;
}


static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "okcoinextra",         "addmonitor",            &addmonitor,      true  },
    { "okcoinextra",         "delmonitor",            &delmonitor,      true  },
    { "okcoinextra",         "ismonitor",             &ismonitor,       true  },
    { "okcoinextra",         "ackmonitor",            &ackmonitor,      true  },
    { "okcoinextra",         "resynctx",              &resynctx,        true  },
    { "okcoinextra",         "ackblock",              &ackblock,        true  },
    { "okcoinextra",         "rescan",                &rescan,          true  }
};

void RegisterOKCoinExtraRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

