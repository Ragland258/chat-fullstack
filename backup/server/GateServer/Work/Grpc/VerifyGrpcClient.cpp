#include "VerifyGrpcClient.h"

VarifyGrpcClient::VarifyGrpcClient()
{
	// 从cofig.ini中读取VarifyService的host和port,poolsize
	auto config = ConfigMgr::GetInstance();
	auto  host = (*config)["VarifyServer"]["Host"];
	auto port = (*config)["VarifyServer"]["Port"];
	auto poolsize = std::stoi((*config)["VarifyServer"]["PoolSize"]);
	pool_.initPool(host, port, poolsize);
}


GetVarifyRsp VarifyGrpcClient::GetVerify(std::string email)
{
	ClientContext context;// Create a new client context for the RPC
	GetVarifyRsp reply;// Create a new GetVarifyRsp object to hold the response �ذ�
	GetVarifyReq request;// Create a new GetVarifyReq object to hold the request ����
	request.set_email(email);//���ó�Ա

	//使用RpcPoolGuard来管理VarifyService的Stub对象，从连接池中获取一个Stub对象
	RpcPoolGuard<VarifyService> stub(pool_);
	if (!stub) {
		std::cout << "[RPC] GetVarifyCode failed, no available stub in the pool" << std::endl;
		reply.set_error(static_cast<int>(ErrorCode::RPC_Error));
		reply.set_email(email);
		return reply;
	}

	Status status = stub->GetVarifyCode(&context, request, &reply);// Call the GetVarifyCode RPC method
	if (status.ok())
	{
		return reply;
	}
	else
	{
		std::cout << "[RPC] GetVarifyCode failed, code: " << status.error_code()
			<< ", message: " << status.error_message() << std::endl;
		reply.set_error(static_cast<int>(ErrorCode::RPC_Error));
		reply.set_email(email);
		return reply;
	}
}
