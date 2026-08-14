#pragma once
#include <memory>
#include "const.h"
#include "../ThreadPool.h"
#include "Work/Mysql/MysqlMgr.h"
#include "Work/Redis/RedisMgr.h"
#include "ConfigMgr.h"
class HttpConnection;

/** 
* 请求处理基类,具体请求的方法实现部分
**/
class RequestHandler
{
public:
	virtual ~RequestHandler() = default;

	RequestHandler(
		const RequestHandler&) = delete;

	RequestHandler& operator=(
		const RequestHandler&) = delete;

	virtual void Handler(
		std::shared_ptr<HttpConnection> connection
	) = 0;
	
protected:
	RequestHandler() = default;

    std::string BuildJsonResponse(
        ErrorCode error,
        const std::string& message,
        const std::string& email = " ",
        const std::string& token = " ",
		const std::string& host = " ",
		const std::string& port = " "
    )
    {
        Json::Value root;
        root["error"] = static_cast<int>(error);
        root["message"] = message;
        if (!email.empty())
            root["email"] = email;
        if (!token.empty())
            root["token"] = token;
        if(!host.empty())
			root["host"] = host;
		if (!port.empty())
			root["port"] = port;

        //让字节变得紧凑
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";

        return Json::writeString(writer, root);
    }
};

