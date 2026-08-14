#pragma once
#include "const.h"
#include "RequestHandler.h"

class RegisterHandler final:public RequestHandler
{
public:

	void Handler(
		std::shared_ptr<HttpConnection> connection) override;
};

