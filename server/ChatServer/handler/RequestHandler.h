#pragma once
#include "const.h"

class Connection;


class RequestHandler
{
public:

	RequestHandler() = default;

	virtual ~RequestHandler() = default;

	RequestHandler(
		const RequestHandler&) = delete;

	RequestHandler& operator=(
		const RequestHandler&) = delete;


	virtual void Handler(
		const Json::Value& data,
		std::shared_ptr<Connection>) = 0;

	bool cheakSession(const string& token);

	bool ParseUint64(
		const Json::Value& value,
		std::uint64_t& result
	);

};

