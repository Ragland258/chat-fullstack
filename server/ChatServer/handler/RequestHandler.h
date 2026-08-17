#pragma once
#include "const.h"
#include "ThreadPool.h"
#include "ConfigMgr.h"
#include "redis/RedisMgr.h"
#include "mysql/MysqlMgr.h"

class Connection;


class RequestHandler
{
public:

	RequestHandler() = default;

	virtual ~RequestHandler() = default;

	RequestHandler(
		const RequestHandler&) = delete;

	RequestHandler& operator=(
		const RequestHandler&) = delete;


	virtual void Handler(
		const Json::Value& data,
		std::shared_ptr<Connection>) = 0;

	bool cheakSession(const string& token);

	bool ParseUint64(
		const Json::Value& value,
		std::uint64_t& result
	);

protected:

	/**
	*
	* type : "search_user_ack",
	* request_id 请求者id: "request-001",
	* error : 错误码  ErrorCode::Success,
	* message : 成功或者错误码信息 "user found",
	* data : 处理结果,可以以json形式发送
	* Version : 版本号
	* 
	*/
	Json::Value BuildJsonRsp(
		const std::string& type,
		const std::string& request_id,
		ErrorCode error_code,
		const std::string& message,
		Json::Value data = Json::Value(),
		int version = 1
	);
};

