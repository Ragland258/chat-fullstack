#pragma once

#include <iostream>

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/basic_random_generator.hpp>
#include <boost/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <json/value.h>
#include <json/json.h>
#include <json/reader.h>

#include <string>
#include <print>
#include <vector>
#include <queue>
#include <map>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>

using std::string;
using std::unique_ptr;
using std::shared_ptr;

namespace asio = boost::asio;
namespace beast = boost::beast;

enum class ErrorCode
{
	// 消息解码错误
	Message_Json_error = 1001,
	Messgae_Parse_Timeout = 1002,

	// 消息处理错误
	Handler_Timeout = 2001,
	Send_Error = 2002,

	//mysql

	//redis

};
