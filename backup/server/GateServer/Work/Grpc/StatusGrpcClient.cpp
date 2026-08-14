#include "StatusGrpcClient.h"

#include "const.h"

#include <chrono>
#include <iostream>

GetChatServerRsp
StatusGrpcClient::GetChatServer(
    const std::string& email,
    const std::string& token)
{
    GetChatServerRsp reply;

    /*
     * Gate Server 本地先检查参数，
     * 避免发送明显无效的 RPC。
     */
    if (email.empty() || token.empty())
    {
        reply.set_error(
            static_cast<int>(
                ErrorCode::Error_Json
                )
        );

        return reply;
    }

    /*
     * 构造 gRPC 请求。
     */
    GetChatServerReq request;

    request.set_email(email);
    request.set_token(token);

    /*
     * 设置调用超时。
     *
     * 防止 Status Server 不可用时，
     * Gate Server 的业务线程长时间阻塞。
     */
    ClientContext context;

    context.set_deadline(
        std::chrono::system_clock::now() +
        std::chrono::seconds(2)
    );

    /*
     * 从现有 RpcPool 中借一个 Stub。
     */
    RpcPoolGuard<StatusService> stub(
        pool_
    );

    if (!stub)
    {
        reply.set_error(
            static_cast<int>(
                ErrorCode::RPC_Error
                )
        );

        return reply;
    }

    /*
     * 发起同步 gRPC 请求。
     */
    const Status status =
        stub->GetChatServer(
            &context,
            request,
            &reply
        );

    /*
     * 检查 RPC 传输层状态。
     */
    if (!status.ok())
    {
        std::cerr
            << "[Status RPC] GetChatServer failed, code: "
            << status.error_code()
            << ", message: "
            << status.error_message()
            << std::endl;

        reply.Clear();

        reply.set_error(
            static_cast<int>(
                ErrorCode::RPC_Error
                )
        );
    }

    return reply;
}

StatusGrpcClient::StatusGrpcClient()
{
		// 从cofig.ini中读取StatusService的host和port,poolsize
	auto config = ConfigMgr::GetInstance();
	auto host = (*config)["StatusServer"]["Host"];
		auto port = (*config)["StatusServer"]["Port"];
	auto poolsize = std::stoi((*config)["StatusServer"]["PoolSize"]);
	pool_.initPool(host, port, poolsize);


}
