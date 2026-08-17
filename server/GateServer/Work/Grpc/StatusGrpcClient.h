#pragma once
#include "ConfigMgr.h"
#include "message.grpc.pb.h"
#include "RpcPool.h"
#include "Singleton.h"

#include <cstdint>

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;

using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::StatusService;


class StatusGrpcClient :public Singleton<StatusGrpcClient>
{
	using Pool = RpcPool<StatusService>;
	friend class Singleton<StatusGrpcClient>;
public:
	~StatusGrpcClient() = default;
	GetChatServerRsp GetChatServer(
		std::uint64_t uid,
		const std::string& email,
		std::uint64_t deviceId
	);



private:
	StatusGrpcClient();
	Pool pool_;
};

