/*
 * rpcaccount.cpp
 *
 *  Created on: 2014年10月28日
 *      Author: Administrator
 */
#include <stdexcept>

#include "init.h"
#include "main.h"
#include "rpcserver.h"
#include "sync.h"
#include "key.h"
#include "base58.h"
#include "wallet.h"
#include "json/json_spirit_value.h"

using namespace json_spirit;
using namespace std;
using std::runtime_error;


json_spirit::Value ackblock(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "ackblock \"requestId\"\n"
            "\ack a monitor request.\n"
            "\nResult\n"
            "\"bool\"      (string) true if success\n"
            "\nExamples\n"
        );

    json_spirit::Array ret;

    json_spirit::Value param = params[0];
    const string requestId = param.get_str();

    return pblockMonitor->ack(requestId);
}
