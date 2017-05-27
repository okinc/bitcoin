#ifndef COKHTTPCLIENT_H
#define COKHTTPCLIENT_H

#include <string>
#include <stdint.h>

#include <univalue.h>


UniValue CallHttpPost(const std::string host, const int port, const std::string url, const std::string& body);

#endif // COKHTTPCLIENT_H
