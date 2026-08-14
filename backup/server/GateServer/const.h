#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/uuid/uuid.hpp>
#include <memory>
#include <iostream>	
#include <functional>
#include <map>
#include <unordered_map>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <stack>
#include <queue>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cassert>
#include <cstddef>
#include <future>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace beast = boost::beast;

enum class ErrorCode
{
	Success = 0,
	Error_Json = 1001,//
	RPC_Error = 1002,

	//redis
	Varify_Expired = 1003,//验证码过期
	Varify_Error = 1004,//验证码错误
	Redis_Error = 1005,
	//mysql
	User_Exist = 2005,//用户已存在
	User_Not_Exist = 2006,//用户不存在
	PassWord_Error = 2007,//密码错误
	No_Emial = 2008,//不存在的邮箱
	PassWord_Update_Error = 2009,    //密码更新错误
	// MySQL / 系统错误
	Mysql_Error = 2010,
	Mysql_Pool_Timeout = 2011,
	Mysql_Result_Error = 2012,
	//哈希
	Password_Hash_Error = 3013,//哈希加密错误

	// statusServer
	Token_Expired = 4001,//token过期
	Token_Error = 4002,//token错误
	Server_Empty = 4003,//服务器列表为空

	//未知错误
	Unknown_Error = 1014
};

class ConfigMgr;


class Defer
{
public:
	Defer(std::function<void()> func):func_(func){}
	~Defer() { func_(); }
private:
	std::function<void()> func_;
};