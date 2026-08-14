#pragma once
#include "handler/RequestHandler.h"

class Connection;
class ErrorHandler final:public RequestHandler
{
	void Handler(
		const Json::Value &data,
		std::shared_ptr<Connection> connection
	) override;

};

