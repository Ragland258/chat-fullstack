#include "LogicRoute.h"

#include "ioLoop/Connection.h"
#include "handler/RequestHandler.h"
#include "handler/soloChatHandler.h"


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
	try
	{
		// 处理消息json
		Json::Reader reader;
		Json::Value src_root;
		auto parse_success = reader.parse(json, src_root);
		if (!parse_success
			|| !src_root.isObject())
		{
			handler_map_["error"]->Handler(
				"JsonError",
				std::move(connection)
			);
		}
		else // 检测type存在
		{
			/** 消息格式:
			* uid
			* token
			* require
			* data
			*/
			if (!src_root.isMember("require")
				|| !src_root["require"].isNull())
			{
				handler_map_["error"]->Handler(
					"JsonError",
					std::move(connection)
				);
				std::cerr << "[LogicRoute]: json no mumber named require\n";
			}
			else // type存在
			{
				auto require = src_root["require"].asString();

				auto it = handler_map_.find(require);

				if (it == handler_map_.end()) // 需要的require不存在
				{
					handler_map_["error"]->Handler(
						"ErrorRequire",
						std::move(connection)
					);
					std::cerr << "[LogicRoute]: require failed\n";
				}
				else
				{
					it->second->Handler(
						std::move(json),
						std::move(connection)
					);
				}
			}
		}

	}
	catch (std::exception& ec)
	{

	}
}

LogicRoute::LogicRoute()
{
	// 单聊
	RegisterHandler(
		"soloChat",
		std::make_unique<soloChatHandler>()
	);

	// 错误处理
	RegisterHandler(
		"error"
	)
}
