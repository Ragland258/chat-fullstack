#include "LogicRoute.h"

#include "ioLoop/Connection.h"
#include "handler/RequestHandler.h"
#include "handler/soloChatHandler.h"
#include "handler/SearchHandler.h"
#include "handler/SetAvatarHandler.h"
LogicRoute::~LogicRoute()
{
}

void LogicRoute::RegisterHandler(const std::string& message_type, std::unique_ptr<RequestHandler> handler)
{
	if (!handler)
		return;
	handler_map_.insert_or_assign(
		message_type,
		std::move(handler)
	);
	
}

bool LogicRoute::HandlerPost(const std::string& json, std::shared_ptr<Connection> connection)
{
	/** 消息格式:
	* uid
	* token
	* messageType
	* data 
	*
	*/
	auto dispatch_error =
		[this, &connection](const std::string& message)
		{
			auto error_handler = handler_map_.find("error");
			if (error_handler == handler_map_.end()
				|| !error_handler->second
				|| !connection)
			{
				return false;
			}

			Json::Value error_data;
			error_data["message"] = message;
			error_handler->second->Handler(
				error_data,
				connection
			);
			return false;
		};

	if (!connection)
	{
		return false;
	}

	try
	{
		// 处理消息json
		Json::Reader reader;
		Json::Value src_root;
		auto parse_success = reader.parse(json, src_root);
		if (!parse_success
			|| !src_root.isObject())
		{
			return dispatch_error("JsonError");
		}

		// 检测 require 是否存在且为字符串。
		if (!src_root.isMember("require")
			|| !src_root["require"].isString())
		{
			std::cerr << "[LogicRoute]: json has no string member named require\n";
			return dispatch_error("JsonError");
		}

		auto require = src_root["require"].asString();
		auto handler = handler_map_.find(require);
		if (handler == handler_map_.end() || !handler->second)
		{
			std::cerr << "[LogicRoute]: require failed\n";
			return dispatch_error("ErrorRequire");
		}

		handler->second->Handler(
			std::move(src_root),
			std::move(connection)
		);
		return true;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "[LogicRoute]: exception: " << exception.what() << '\n';
		return dispatch_error("InternalError");
	}
}

LogicRoute::LogicRoute()
{
	// 单聊
	RegisterHandler(
		"soloChat",
		std::make_unique<soloChatHandler>()
	);

	// 查找用户
	RegisterHandler(
		"searchUser",
		std::make_unique<SearchHandler>()
	);

	// 设置头像
	RegisterHandler(
		"setAvatar",
		std::make_unique<SetAvatarHandler>()
	);
}
