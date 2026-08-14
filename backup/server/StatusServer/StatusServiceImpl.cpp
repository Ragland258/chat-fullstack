#include "StatusServiceImpl.h"

#include "RedisMgr.h"

#include <iostream>
#include <mutex>
#include <string>

StatusServiceImpl::StatusServiceImpl()
{
	LoadChatServer("ChatServer1");
	LoadChatServer("ChatServer2");
}

grpc::Status StatusServiceImpl::GetChatServer(
    grpc::ServerContext* context,
    const message::GetChatServerReq* request,
    message::GetChatServerRsp* reply)
{
    /*
     * 防御性检查。
     */
    if (request == nullptr ||
        reply == nullptr)
    {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "request or reply is null"
        );
    }

    /*
     * 调用已经被取消。
     */
    if (context != nullptr &&
        context->IsCancelled())
    {
        return grpc::Status(
            grpc::StatusCode::CANCELLED,
            "request cancelled"
        );
    }

    /*
     * 获取 Gate Server 传来的邮箱和 Token。
     */
    const std::string& email =
        request->email();

    const std::string& token =
        request->token();

    /*
     * 检查参数。
     */
    if (email.empty() || token.empty())
    {
        reply->set_error(
            static_cast<int>(
                ErrorCode::Error_Json
                )
        );

        return grpc::Status::OK;
    }

    /*
     * 去 Redis 查询：
     *
     * login:token:<email>
     *
     * 并比较 Redis Token 与请求 Token。
     */
    const bool tokenValid =
        RedisMgr::GetInstance()
        ->VerifyLoginToken(
            email,
            token
        );

    if (!tokenValid)
    {
        reply->set_error(
            static_cast<int>(
                ErrorCode::Token_Error
                )
        );

        return grpc::Status::OK;
    }

    /*
     * 没有配置任何 Chat Server。
     */
    if (_servers.empty())
    {
        reply->set_error(
            static_cast<int>(
				ErrorCode::Server_Empty)
        );
    }

    ChatServer selectedServer;

    {
        /*
         * 同步 gRPC Server 可能并发调用此函数，
         * 轮询下标需要加锁。
         */
        std::lock_guard<std::mutex> lock(
            _server_mutex
        );

        selectedServer =
            _servers[_server_index];

        _server_index =
            (_server_index + 1) %
            _servers.size();
    }

    /*
     * Token 验证成功后才返回 Chat Server。
     */
    reply->set_error(
        static_cast<int>(
            ErrorCode::Success
            )
    );

    reply->set_ip(
        selectedServer.host
    );

    reply->set_port(
        std::stoi(selectedServer.port)
    );

    /*
     * Status Server 不生成、不返回 Token。
     */
    std::cout
        << "Assigned email "
        << email
        << " to Chat Server "
        << selectedServer.host
        << ":"
        << selectedServer.port
        << std::endl;

    return grpc::Status::OK;
}

void StatusServiceImpl::LoadChatServer(const std::string& server)
{
    auto config = ConfigMgr::GetInstance();
	auto host = (*config)[server]["Host"];
	auto port = (*config)[server]["Port"];
    if (host.empty() || port.empty())
    {
        std::cerr
            << "ChatServer Host or Port is empty"
            << std::endl;
        return;
    }
    ChatServer chatServer;
    chatServer.host = host;
    chatServer.port = port;
	_servers.push_back(chatServer);
}
