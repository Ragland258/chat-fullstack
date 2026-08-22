#pragma once

#include "grpcpp/grpcpp.h"
#include "generate/server.grpc.pb.h"
#include "const.h"

#include <string>
#include <string_view>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using namespace fileserver::v1;

class RpcServreImpl : public FileService::Service
{
public:
	RpcServreImpl();

	// 校验上传请求，返回对象键和 MinIO 预签名 PUT URL。
	grpc::Status InitUpload(
		::grpc::ServerContext* context,
		const ::fileserver::v1::InitUploadReq* request,
		::fileserver::v1::InitUploadRsp* response) override;

	// 客户端上传完成后，向 MinIO 查询并确认文件可用。
	Status CompleteUpload(
		ServerContext* context,
		const CompleteUploadReq* request,
		CompleteUploadRsp* response) override;

	// 校验访问权限后，返回短期有效的预签名 GET URL。
	Status GetDownloadUrl(
		ServerContext* context,
		const GetDownloadUrlReq* request,
		GetDownloadUrlRsp* response) override;

private:
	// 生成不包含用户输入的随机 UUID，用于构造对象键。
	std::string GenerateFileId();

	void FillResult(
		FileResult* result,
		FileResultCode code,
		const std::string_view message);
};

