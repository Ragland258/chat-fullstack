#include "StatusServiceImpl.h"

#include "RedisMgr.h"

#include <iostream>
#include <stdexcept>

StatusServiceImpl::StatusServiceImpl()
{
    const auto config = ConfigMgr::GetInstance();
    const std::string configuredTtl = (*config)["RedisServer"]["TTL"];

    if (!configuredTtl.empty())
    {
        try
        {
            const int ttl = std::stoi(configuredTtl);
            if (ttl > 0)
                _session_ttl_seconds = ttl;
        }
        catch (const std::exception& exception)
        {
            std::cerr
                << "Invalid Redis session TTL, using default: "
                << exception.what()
                << std::endl;
        }
    }

    LoadChatServer("ChatServer1");
    LoadChatServer("ChatServer2");
}

grpc::Status StatusServiceImpl::GetChatServer(
    grpc::ServerContext* context,
    const message::GetChatServerReq* request,
    message::GetChatServerRsp* reply)
{
    if (request == nullptr || reply == nullptr)
    {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "request or reply is null");
    }

    if (context != nullptr && context->IsCancelled())
    {
        return grpc::Status(
            grpc::StatusCode::CANCELLED,
            "request cancelled");
    }

    const std::uint64_t uid = request->uid();
    const std::uint64_t deviceId = request->device_id();
    const std::string email = request->email();

    if (uid == 0 || deviceId == 0 || email.empty())
    {
        reply->set_error(static_cast<int>(ErrorCode::Error_Json));
        return grpc::Status::OK;
    }

    ChatServer selectedServer;

    {
        std::lock_guard<std::mutex> lock(_server_mutex);

        if (_servers.empty())
        {
            reply->set_error(static_cast<int>(ErrorCode::Server_Empty));
            return grpc::Status::OK;
        }

        selectedServer = _servers[_server_index];
        _server_index = (_server_index + 1) % _servers.size();
    }

    const std::string token = RedisMgr::GetInstance()->CreateLoginSession(
        uid,
        deviceId,
        email,
        selectedServer.server_id,
        _session_ttl_seconds);

    if (token.empty())
    {
        reply->set_error(static_cast<int>(ErrorCode::Redis_Error));
        return grpc::Status::OK;
    }

    reply->set_error(static_cast<int>(ErrorCode::Success));
    reply->set_ip(selectedServer.host);
    reply->set_port(selectedServer.port);
    reply->set_token(token);
    reply->set_server_id(selectedServer.server_id);

    std::cout
        << "Assigned uid=" << uid
        << ", device_id=" << deviceId
        << " to " << selectedServer.server_id
        << " (" << selectedServer.host
        << ":" << selectedServer.port << ")"
        << std::endl;

    return grpc::Status::OK;
}

void StatusServiceImpl::LoadChatServer(const std::string& server)
{
    const auto config = ConfigMgr::GetInstance();
    const std::string host = (*config)[server]["Host"];
    const std::string portText = (*config)[server]["Port"];

    if (server.empty() || host.empty() || portText.empty())
    {
        std::cerr
            << server << " Host or Port is empty"
            << std::endl;
        return;
    }

    try
    {
        const int port = std::stoi(portText);
        if (port <= 0 || port > 65535)
            throw std::out_of_range("port must be in range 1..65535");

        _servers.push_back(ChatServer{server, host, port});
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "Invalid " << server << " port: "
            << exception.what()
            << std::endl;
    }
}
