#include "ErrorHandler.h"
#include "ioLoop/Connection.h"

void ErrorHandler::Handler(
	const Json::Value& data,
	std::shared_ptr<Connection> connection)
{
	if (!connection)
	{
		return;
	}

	const auto message =
		data.isMember("message") && data["message"].isString()
		? data["message"].asString()
		: std::string("UnknownError");

	auto response = connection->JsonResponse(
		ErrorCode::Message_Json_error,
		message
	);
	connection->SendResponse(std::move(response));
}
