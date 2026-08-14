#pragma once
#include "RequestHandler.h"

class LoginHandler final : public RequestHandler
{
	void Handler(
		std::shared_ptr<HttpConnection> connection
	) override;
};

