#pragma once
#include "RequestHandler.h"
#include "const.h"

class VarifyHandler final :public RequestHandler
{
	void Handler(std::shared_ptr<HttpConnection> conn) override;
};

