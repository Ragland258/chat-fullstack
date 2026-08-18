#pragma once

#include "grpcpp/grpcpp.h"
#include "generate/server.grpc.pb.h"
#include "const.h"


using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using namespace fileserver::v1;

class RpcServreImpl : public FileService::Service
{
public:
	RpcServreImpl();

	// 初始化上传 生成file_id,并且返回minio预签名上传url
	grpc::Status InitUpload(
		::grpc::ServerContext* context, const ::fileserver::v1::InitUploadReq*request, 
		::fileserver::v1::InitUploadRsp* response
	)override;

	// 完成上传: 客户端上传完成后,效验并标记文件可用
	Status CompleteUpload(
		ServerContext* context,
		const CompleteUploadReq* request,
		CompleteUploadRsp* response
	)override;

	// 获取下载url:返回短期有效的下载地址
	Status GetDownloadUrl(
		ServerContext* context,
		const GetDownloadUrlReq* request,
		GetDownloadUrlRsp* response
	)override;

private:
	
	//
	std::string GenerateFileId();


	void FillResult(
		FileResult* result,
		FileResultCode code,
		const std::string_view message
	);
};

