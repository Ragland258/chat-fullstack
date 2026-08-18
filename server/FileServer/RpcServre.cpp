#include "RpcServre.h"

RpcServreImpl::RpcServreImpl()
{
}

grpc::Status RpcServreImpl::InitUpload(::grpc::ServerContext* context, const::fileserver::v1::InitUploadReq* request, ::fileserver::v1::InitUploadRsp* response)
{
	// 1
	if (request == nullptr || response == nullptr)
	{	
		return Status{ grpc::StatusCode::INTERNAL,
		"request or response id null!" };
	}

	// 2.客户端可能取消
	if (context == nullptr || context->IsCancelled())
	{
		return Status{ grpc::StatusCode::CANCELLED,
		"request cancelled!" };
	}

	// 3.
	if (request->uploader_id() == 0 ||
		request->file_name().empty() ||
		request->size_bytes() == 0)
	{
		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INVALID_REQUEST,
			"upload_id, file_name, size_byte is required"
		);

		// 错误码在response.result里面
		return Status::OK;

	}

	// 4. 生成file_id
	std::string file_id = GenerateFileId();

	// 5. 调用 miniomgr 生成预签名 上传url


	//

}

void RpcServreImpl::FillResult(
	FileResult* result, 
	FileResultCode code, 
	const std::string_view message)
{
	if (result)
	{
		result->set_code(code);
		result->set_message(message);
	}
}
