#include "soloChatHandler.h"

void soloChatHandler::Handler(
	const Json::Value& message,
	std::shared_ptr<Connection> connection)
{
	static_cast<void>(message);
	static_cast<void>(connection);
}
