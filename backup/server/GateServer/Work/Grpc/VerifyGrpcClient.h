#pragma once
#include "const.h"
#include "RpcPool.h"
#include "Singleton.h"
#include "ConfigMgr.h"


using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::VarifyService;
using message::GetVarifyReq;
using message::GetVarifyRsp;

class VarifyGrpcClient :public Singleton<VarifyGrpcClient>
{
	friend class Singleton<VarifyGrpcClient>;
	using Pool = RpcPool<VarifyService>;

public:
	GetVarifyRsp GetVerify(std::string email);
	~VarifyGrpcClient(){}
	VarifyGrpcClient(const VarifyGrpcClient&) = delete;
	VarifyGrpcClient& operator=(const VarifyGrpcClient&) = delete;


private:
	VarifyGrpcClient();
	Pool pool_;
};

