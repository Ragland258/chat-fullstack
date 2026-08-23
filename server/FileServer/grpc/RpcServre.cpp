#include "RpcServre.h"

#include "ConfigMgr.h"
#include "minio/minioMgr.h"

#include <chrono>
#include <cstdint>
#include <exception>

namespace
{
	// 当前头像业务允许的最大文件大小：5 MiB。
	constexpr std::uint64_t kMaxAvatarBytes =
		5ULL * 1024ULL * 1024ULL;

	// MinIO 预签名 URL 最长允许 7 天有效期。
	constexpr unsigned long kMaxPresignedUrlTtlSeconds =
		7UL * 24UL * 60UL * 60UL;

	// 根据已允许的 MIME 类型返回由服务端控制的安全扩展名。
	std::string AvatarExtension(const std::string& content_type)
	{
		if (content_type == "image/jpeg")
			return ".jpg";

		if (content_type == "image/png")
			return ".png";

		if (content_type == "image/webp")
			return ".webp";

		return {};
	}

	std::int64_t CurrentTimeMilliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();
	}
}

RpcServreImpl::RpcServreImpl()
{
}

grpc::Status RpcServreImpl::InitUpload(
	::grpc::ServerContext* context,
	const ::fileserver::v1::InitUploadReq* request,
	::fileserver::v1::InitUploadRsp* response)
{
	// 空指针属于 RPC 框架层错误，直接返回 gRPC INTERNAL。
	if (request == nullptr || response == nullptr)
	{
		return Status{
			grpc::StatusCode::INTERNAL,
			"request or response is null"
		};
	}

	// 客户端已经取消请求时，不再创建无用的上传地址。
	if (context == nullptr || context->IsCancelled())
	{
		return Status{
			grpc::StatusCode::CANCELLED,
			"request cancelled"
		};
	}

	// 基础字段缺失属于业务错误，gRPC 仍返回 OK，由 FileResult 表达失败。
	if (request->uploader_id() == 0 ||
		request->file_name().empty() ||
		request->content_type().empty() ||
		request->size_bytes() == 0)
	{
		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INVALID_REQUEST,
			"uploader_id, file_name, content_type and size_bytes are required");

		return Status::OK;
	}

	// 当前阶段只开放头像上传，其他用途后续再分别制定权限和大小限制。
	if (request->purpose() != FilePurpose::FILE_PURPOSE_AVATAR)
	{
		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INVALID_REQUEST,
			"only avatar upload is currently supported");

		return Status::OK;
	}

	if (request->size_bytes() > kMaxAvatarBytes)
	{
		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INVALID_REQUEST,
			"avatar exceeds the 5 MiB limit");

		return Status::OK;
	}

	const std::string extension =
		AvatarExtension(request->content_type());

	if (extension.empty())
	{
		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INVALID_REQUEST,
			"avatar content type must be image/jpeg, image/png or image/webp");

		return Status::OK;
	}

	/*
	 * 不使用客户端文件名作为 MinIO 路径，避免重名和路径注入。
	 * file_id 直接返回完整对象键，使 CompleteUpload/GetDownloadUrl
	 * 不依赖进程内临时映射，服务重启后仍能定位对象。
	 */
	const std::string object_key =
		"avatar/" +
		std::to_string(request->uploader_id()) +
		"/" +
		GenerateFileId() +
		extension;

	unsigned int ttl_seconds = 0;

	try
	{
		const std::string ttl_text =
			(*ConfigMgr::GetInstance())["Minio"]["UploadUrlTTL"];

		std::size_t parsed_count = 0;
		const unsigned long parsed_ttl =
			std::stoul(ttl_text, &parsed_count);

		if (parsed_count != ttl_text.size() ||
			parsed_ttl == 0 ||
			parsed_ttl > kMaxPresignedUrlTtlSeconds)
		{
			throw std::out_of_range(
				"UploadUrlTTL is out of range");
		}

		ttl_seconds =
			static_cast<unsigned int>(parsed_ttl);
	}
	catch (const std::exception& exception)
	{
		std::cerr
			<< "[FileServer] invalid Minio.UploadUrlTTL: "
			<< exception.what()
			<< std::endl;

		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INTERNAL_ERROR,
			"upload URL TTL configuration is invalid");

		return Status::OK;
	}

	const auto upload_result =
		minioMgr::GetInstance()->CreateUploadUrl(
			object_key,
			ttl_seconds);

	if (!upload_result.success)
	{
		std::cerr
			<< "[FileServer] failed to create upload URL: "
			<< upload_result.error_message
			<< std::endl;

		FillResult(
			response->mutable_result(),
			FileResultCode::FILE_RESULT_INTERNAL_ERROR,
			"failed to create upload URL");

		return Status::OK;
	}

	response->set_file_id(object_key);
	response->set_upload_url(upload_result.url);

	// 客户端 PUT 上传时应携带与声明一致的 Content-Type。
	(*response->mutable_upload_headers())
		["Content-Type"] = request->content_type();

	response->set_expires_at_ms(
		CurrentTimeMilliseconds() +
		static_cast<std::int64_t>(ttl_seconds) * 1000);

	FillResult(
		response->mutable_result(),
		FileResultCode::FILE_RESULT_OK,
		"upload initialized");

	return Status::OK;
}

Status RpcServreImpl::CompleteUpload(ServerContext* context, const CompleteUploadReq* request, CompleteUploadRsp* response)
{
	return Status();
}

Status RpcServreImpl::GetDownloadUrl(ServerContext* context, const GetDownloadUrlReq* request, GetDownloadUrlRsp* response)
{
	return Status();
}

std::string RpcServreImpl::GenerateFileId()
{
	// UUID 只用于生成不可预测且低冲突的对象名，不包含用户输入。
	boost::uuids::random_generator generator;
	const boost::uuids::uuid uuid =
		generator();

	return boost::uuids::to_string(uuid);
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
