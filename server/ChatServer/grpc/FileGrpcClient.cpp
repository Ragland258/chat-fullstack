#include "FileGrpcClient.h"

#include "ConfigMgr.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <utility>

InitUploadRpcResult FileGrpcClient::InitUpload(
    const fileserver::v1::InitUploadReq& request)
{
    fileserver::v1::InitUploadRsp response;

    /*
     * ClientContext 保存单次调用的超时、元数据和取消状态。
     * 它不能跨 RPC 复用，因此每次调用都要重新创建。
     */
    grpc::ClientContext context;

    // 防止 FileServer 故障时永久占用 ChatServer 的工作线程。
    context.set_deadline(
        std::chrono::system_clock::now() +
        std::chrono::seconds(2));

    /*
     * 从池中借出 FileService Stub。
     * 函数退出时，RpcPoolGuard 会自动将它归还。
     */
    RpcPoolGuard<FileService> stub(pool_);

    if (!stub)
    {
        return InitUploadRpcResult{
            grpc::Status{
                grpc::StatusCode::UNAVAILABLE,
                "FileServer gRPC pool is stopped"
            },
            std::move(response)
        };
    }

    // 同步等待 FileServer 返回；该函数应当在业务线程中调用。
    grpc::Status status =
        stub->InitUpload(
            &context,
            request,
            &response);

    if (!status.ok())
    {
        std::cerr
            << "[File RPC] InitUpload failed, code: "
            << status.error_code()
            << ", message: "
            << status.error_message()
            << std::endl;
    }

    return InitUploadRpcResult{
        std::move(status),
        std::move(response)
    };
}

FileGrpcClient::FileGrpcClient()
{
    const auto config =
        ConfigMgr::GetInstance();

    const std::string host =
        (*config)["FileServer"]["Host"];

    // INI 键区分大小写，这里必须与配置文件中的 Port 一致。
    const std::string port =
        (*config)["FileServer"]["Port"];

    const std::string pool_size =
        (*config)["FileServer"]["PoolSize"];

    if (host.empty() ||
        port.empty() ||
        pool_size.empty())
    {
        throw std::runtime_error(
            "FileServer configuration is incomplete");
    }

    const int poolSize =
        std::stoi(pool_size);

    if (poolSize <= 0)
    {
        throw std::runtime_error(
            "FileServer PoolSize must be greater than zero");
    }

    /*
     * 池中的 Stub 共享同一个 gRPC Channel，
     * PoolSize 用来控制同时借出的 Stub 数量。
     */
    pool_.initPool(
        host,
        port,
        static_cast<std::size_t>(poolSize));
}
