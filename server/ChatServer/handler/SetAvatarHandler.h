#pragma once
#include "handler/RequestHandler.h"

class Connection;

class SetAvatarHandler final
	: public RequestHandler
{
	void Handler(
		const Json::Value& json,
		std::shared_ptr<Connection> connection
	) override;
};

