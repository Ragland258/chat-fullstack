#include "SetAvatarHandler.h"
#include "ioLoop/Connection.h"
#include "grpc/FileGrpcClient.h"
#include "ThreadPool.h"


/*
* {
* 	"version": 1,
* 		"require" : "setAvatar",
* 		"action" : "initUpload",
* 		"request_id" : "avatar-request-001",
* 		"data" : {
* 		"client_file_id": "550e8400-e29b-41d4-a716-446655440000",
* 			"size_bytes" : 26931,
* 			"content_type" : "image/png",
* 			"file_name" : "avatar.png"
* 	}
* }
*
**/

void SetAvatarHandler::Handler(const Json::Value& json, std::shared_ptr<Connection> connection)
{
	if (!connection)
		return;

	// 验证json
	if (!connection->IsAuthenticated() ||
		connection->GetUserId() == 0)
	{
		Json::Value response =
			this->BuildJsonRsp(
				"set_avatar_ack",
				"",
				ErrorCode::Unaauthorized,
				"user is not authenticated"
			);
		connection->SendResponse(response.asString());

		return;
	}

	const string requestId =
		json["request_id"].asString();

	/*
    * SetAvatarHandler 后续还会处理 completeUpload，
    * 所以使用 action 区分上传阶段。
    */

	if (!json.isMember("action") ||
		!json["action"].isString() ||
		json["action"].asString() != "initUpload")
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"action must be initUpload");

		connection->SendResponse(
			response.asString());

		return;
	}

	if (!json.isMember("data") ||
		!json["data"].isObject())
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"data is missing or invalid");

		connection->SendResponse(
			response.asString());

		return;
	}

	const Json::Value& data =
		json["data"];

	// 验证文件名
	if (!data.isMember("file_name") ||
		!data["file_name"].isString() ||
		data["file_name"].asString().empty())
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"file_name is missing or invalid");

		connection->SendResponse(
			response.asString());

		return;
	}

	/*
	* 验证 MIME 类型。
	* FileServer 还会再次校验，这里是为了尽早拒绝错误请求。
	*/
	if (!data.isMember("content_type") ||
		!data["content_type"].isString() ||
		data["content_type"].asString().empty())
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"content_type is missing or invalid");

		connection->SendResponse(
			response.asString());

		return;
	}

	// 验证uint64,头像最大5mb
	if (!data.isMember("size_bytes") ||
		!data["size_bytes"].asUInt64() ||
		data["size_bytes"].asUInt64() == 0)
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"size_bytes is missing or invalid");

		connection->SendResponse(
			response.asString());

		return;
	}

	// 不用验证客户端字段名
	string clientFileId;

	const string fileName =
		data["file_name"].asString();

	const string ContentType =
		data["conten_type"].asString();

	const std::uint64_t sizeBytes =
		data["size_bytes"].asUInt64();

	if (data.isMember("client_file_id") &&
		data["client_file_id"].isString())
	{
		// 不用验证客户端字段名
		clientFileId =
			data["client_file_id"].asString();
	}
	// uid来自于connection,
	const std::uint64_t uploaderId =
		connection->GetUserId();

	std::weak_ptr<Connection> weakConn{ connection };

	ThreadPool::GetInstance()->commit(
		[this,
		weakConn,
		requestId,
		clientFileId = std::move(clientFileId),
		ContentType,
		fileName,
		sizeBytes,
		uploaderId]()
		{
			try
			{
				fileserver::v1::InitUploadReq request;

				request.set_request_id(requestId);
				request.set_client_file_id(clientFileId);
				request.set_uploader_id(uploaderId);
				request.set_file_name(fileName);
				request.set_size_bytes(sizeBytes);
				request.set_content_type(ContentType);
				request.set_purpose(
					fileserver::v1::FILE_PURPOSE_AVATAR
				);

				// 同步grpc
				InitUploadRpcResult rpc_result =
					FileGrpcClient::GetInstance()->InitUpload(request);

				auto connection =
					weakConn.lock();

				if (!connection)
					return;

				// 1.判断grpc传输是否成功
				if (!rpc_result.TransportOk())
				{
					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::File_Rpc_Error,
							"FileServer is unavailable");

					connection->SendResponse(
						response.asString());

					return;
				}

				// 2.判断file server业务是否成功
				const auto& fileResponse =
					rpc_result.response;
				
				if (!fileResponse.has_result() ||
					fileResponse.result().code() !=
					fileserver::v1::FILE_RESULT_OK)
				{
					Json::Value errorData{ Json::objectValue };

					if (fileResponse.has_result())
					{
						errorData["file_error_code"] =
							static_cast<int>(fileResponse.result().code());
					}

					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::File_Request_Error,
							fileResponse.has_result()
							? fileResponse.result().message()
							: "FileServer returned no result",
							std::move(errorData)
						);

					connection->SendResponse(
						response.asString());

					return;
				}

				// 3.把file server的上传信息转成websocket json
				Json::Value responseData{ Json::objectValue };
				responseData["file_id"] =
					fileResponse.file_id();

				responseData["upload_url"] =
					fileResponse.upload_url();

				responseData["expires_at_ms"] =
					Json::Int64(fileResponse.expires_at_ms());

				Json::Value uploadHeaders{ Json::objectValue };

				for (const auto& head : fileResponse.upload_headers())
				{
					uploadHeaders[head.first] = head.second;
				}

				responseData["upload_headers"] =
					std::move(uploadHeaders);

				auto response = this->BuildJsonRsp(
					"set_avatar_ack",
					requestId,
					ErrorCode::Success,
					"Upload Url Create",
					std::move(responseData)
				);

				connection->SendResponse(response.asString());

			}
			catch (const std::exception& exception)
			{
				std::cerr
					<< "[SetAvatarHandler] exception: "
					<< exception.what()
					<< std::endl;

				auto currentConnection =
					weakConn.lock();

				if (!currentConnection)
					return;

				Json::Value response =
					BuildJsonRsp(
						"set_avatar_ack",
						requestId,
						ErrorCode::File_Rpc_Error,
						"internal file service error");

				currentConnection->SendResponse(
					response.asString());
			}
		}
	);
}
