#pragma once
#include "RequestHandler.h"

class ResetHandler final : public RequestHandler
{
	void Handler(
		std::shared_ptr<HttpConnection> connection
	) override;
};

