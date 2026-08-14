#pragma once
#include "RequestHandler.h"


class soloChatHandler final: public RequestHandler
{
public:

	void Handler(
		const Json::Value& message,
		std::shared_ptr<Connection> connection) override;
};

