#pragma once

#include "generate/server.grpc.pb.h"
#include "RpcPool.h"
#include "Singleton.h"

/*
 * 一次 InitUpload 调用包含两层结果：
 *
 * 1. grpc_status 表示连接、超时等传输层结果；
 * 2. response.result() 表示文件格式、大小等业务结果。
 */
struct InitUploadRpcResult
{
    grpc::Status grpc_status;
    fileserver::v1::InitUploadRsp response;

    // 这里只判断 RPC 是否成功到达 FileServer，不代表业务一定成功。
    [[nodiscard]]
    bool TransportOk() const noexcept
    {
        return grpc_status.ok();
    }
};

class FileGrpcClient final
    : public Singleton<FileGrpcClient>
{
    friend class Singleton<FileGrpcClient>;

    using FileService =
        fileserver::v1::FileService;

    using Pool =
        RpcPool<FileService>;

public:
    ~FileGrpcClient() = default;

    FileGrpcClient(const FileGrpcClient&) = delete;
    FileGrpcClient& operator=(const FileGrpcClient&) = delete;

    /*
     * 调用 FileServer::InitUpload。
     * request 由 Handler 构造，响应中包含 file_id 和 upload_url。
     */
    InitUploadRpcResult InitUpload(
        const fileserver::v1::InitUploadReq& request);

private:
    FileGrpcClient();

private:
    Pool pool_;
};
