#pragma once
#include "const.h"
#include "Singleton.h"

class RequestHandler;
class Connection;
class LogicRoute: public Singleton<LogicRoute>
{
	friend class Singleton<LogicRoute>;
public:

	~LogicRoute();

	LogicRoute(const LogicRoute&) = delete;
	LogicRoute& operator=(const LogicRoute&) = delete;

	// 注册路由
	void RegisterHandler(
		const std::string& message_type,
		std::unique_ptr<RequestHandler> handler);
	
	// 分发路由
	bool HandlerPost(
		const std::string& json,
		std::shared_ptr<Connection> connection
	);

private:
	LogicRoute();

private:
	std::unordered_map<std::string, std::unique_ptr<RequestHandler>> handler_map_;
};

