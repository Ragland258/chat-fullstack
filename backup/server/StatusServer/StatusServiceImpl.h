#pragma once
#include "ConfigMgr.h"
#include "const.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"


/*
 * ChatServer
 *
 * 用于保存一台聊天服务器的连接信息。
 */
struct ChatServer
{
    // 聊天服务器地址，例如：127.0.0.1
    std::string host;

    // 聊天服务器端口，例如：8081
    std::string port;
};

/*
 * StatusServiceImpl
 *
 * StatusService 的具体实现类。
 *
 * message::StatusService::Service 是由 message.proto 自动生成的
 * gRPC 服务端基类。
 *
 * 客户端调用 GetChatServer 时，本服务会：
 *
 * 1. 从聊天服务器列表中选择一台服务器；
 * 2. 将服务器地址和端口返回给客户端；
 * 3. 为客户端生成一个随机 Token；
 * 4. 使用轮询方式实现简单负载均衡。
 */

class StatusServiceImpl final : public message::StatusService::Service
{
public:
    /*
     * 构造函数
     *
     * 创建 StatusServiceImpl 对象时，从配置文件读取聊天服务器信息。
     */
    StatusServiceImpl();

    /*
     * GetChatServer
     *
     * 客户端通过该 RPC 接口获取一台聊天服务器。
     *
     * 参数：
     * context：
     *     本次 RPC 请求的上下文。
     *     可以从中获取客户端信息、超时信息以及取消状态。
     *
     * request：
     *     客户端发送的请求对象。
     *
     * reply：
     *     服务端返回给客户端的响应对象。
     *
     * 返回值：
     *     grpc::Status::OK 表示 RPC 调用正常完成。
     */
    grpc::Status GetChatServer(
        grpc::ServerContext* context,
        const message::GetChatServerReq* request,
        message::GetChatServerRsp* reply) override;

private:

    void LoadChatServer(const std::string& server);

    /*
     * 聊天服务器列表。
     *
     * 构造函数会从配置文件中读取服务器信息，
     * 然后保存到该容器中。
     */
    std::vector<ChatServer> _servers;

    /*
     * 下一次需要选择的服务器下标。
     *
     * 假设有两台服务器：
     *
     * 第一次请求：选择 _servers[0]
     * 第二次请求：选择 _servers[1]
     * 第三次请求：重新选择 _servers[0]
     */
    std::size_t _server_index;

    /*
     * 用于保护 _server_index。
     *
     * gRPC 服务端可能同时处理多个客户端请求，
     * GetChatServer 可能被多个线程同时调用。
     *
     * 如果不加锁，多个线程同时修改 _server_index，
     * 会产生数据竞争。
     */
    std::mutex _server_mutex;
};
