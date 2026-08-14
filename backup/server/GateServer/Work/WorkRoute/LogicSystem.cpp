#include "LogicSystem.h"

#include "IoLoop/HttpConnection.h"
#include "Work/WorkHandler/RegisterHandler.h"
#include "Work/WorkHandler/RequestHandler.h"
#include "Work/WorkHandler/VarifyHandler.h"
#include "Work/Grpc/VerifyGrpcClient.h"
#include "Work/WorkHandler/ResetHandler.h"
#include "Work/WorkHandler/LoginHandler.h"

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/ostream.hpp>
#include <boost/beast/http.hpp>
#include <json/json.h>

#include <iostream>
#include <utility>

namespace beast = boost::beast;
namespace http = boost::beast::http;

LogicSystem::~LogicSystem() = default;


void LogicSystem::RegisterPostHandler(
    const std::string& url,
    std::unique_ptr<RequestHandler> handler)
{
    if (!handler)
    {
        return;
    }

    post_request_handlers_.insert_or_assign(
        url,
        std::move(handler));
}

bool LogicSystem::HandlePost(
    const std::string& url,
    std::shared_ptr<HttpConnection> connection)
{
    // 新的类式 Handler 优先。
    const auto handlerIter =
        post_request_handlers_.find(url);

    if (handlerIter != post_request_handlers_.end())
    {
        handlerIter->second->Handler(
            std::move(connection));

        return true;
    }

    return false;
}

LogicSystem::LogicSystem()
{
    // POST /get_verify
    RegisterPostHandler(
        "/get_verify",
        std::make_unique<VarifyHandler>()
    );

    // POST /register 交给 RegisterHandler 处理。
    RegisterPostHandler(
        "/register",
        std::make_unique<RegisterHandler>());

	// POST /reset 交给 ResetHandler 处理。
    RegisterPostHandler(
        "/reset",
		std::make_unique<ResetHandler>());

	// POST /login 交给 LoginHandler 处理。
    RegisterPostHandler(
        "/login",
		std::make_unique<LoginHandler>());
}
