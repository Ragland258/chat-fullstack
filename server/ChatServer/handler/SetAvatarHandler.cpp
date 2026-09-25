#include "SetAvatarHandler.h"
#include "ioLoop/Connection.h"
#include "grpc/FileGrpcClient.h"
#include "ThreadPool.h"
#include "redis/RedisFileMgr.h"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace
{
	/*
	 * 将上传失败原因转换成稳定的协议字符串。
	 *
	 * 这里的字符串既会返回给客户端，也必须与 Redis 中保存的
	 * failure_reason 保持一致，不能随意修改拼写。
	 */
	std::string UploadFailureReasonToText(
		UploadFailureReason reason)
	{
		switch (reason)
		{
		case UploadFailureReason::ObjectNotFound:
			return "object_not_found";

		case UploadFailureReason::SizeMismatch:
			return "size_mismatch";

		case UploadFailureReason::ContentTypeMismatch:
			return "content_type_mismatch";

		case UploadFailureReason::EtagMismatch:
			return "etag_mismatch";

		case UploadFailureReason::UploadExpired:
			return "upload_expired";

		default:
			return "unknown";
		}
	}

	/*
	 * ready/failed 终态会话继续保留一小时，供客户端在回包丢失时
	 * 幂等重试 CompleteUpload，也便于排查最近一次上传失败原因。
	 */
	constexpr auto kUploadTerminalRetentionTtl =
		std::chrono::seconds{ 3600 };
}

void SetAvatarHandler::Handler(const Json::Value& json, 
	std::shared_ptr<Connection> connection)
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
				ErrorCode::Unauthorized,
				"user is not authenticated"
			);
		connection->SendResponse(response.asString());

		return;
	}

	if (!json.isMember("request_id") ||
		!json["request_id"].isString() ||
		json["request_id"].asString().empty())
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				"",
				ErrorCode::Message_Json_error,
				"request_id is missing or invalid");

		connection->SendResponse(
			response.asString());

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
		json["action"].asString().empty())
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"action is missing or invalid");

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

	/*
	 * 公共字段校验完成后立即按上传阶段分发。
	 * 阶段专属字段必须放进对应函数，避免 completeUpload
	 * 被 initUpload 的文件参数校验提前拒绝。
	 */
	const string action =
		json["action"].asString();

	if (action == "initUpload")
	{
		InitUpload(
			data,
			requestId,
			std::move(connection));

		return;
	}

	if (action == "completeUpload")
	{
		CompleteUpload(
			data,
			requestId,
			std::move(connection));

		return;
	}

	Json::Value response =
		BuildJsonRsp(
			"set_avatar_ack",
			requestId,
			ErrorCode::Message_Json_error,
			"unsupported action");

	connection->SendResponse(
		response.asString());
}

void SetAvatarHandler::InitUpload(
	const Json::Value& data,
	const string& requestId,
	std::shared_ptr<Connection> connection)
{
	if (!connection)
		return;

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
	constexpr std::uint64_t kMaxAvatarSizeBytes =
		5ULL * 1024ULL * 1024ULL;

	if (!data.isMember("size_bytes") ||
		!data["size_bytes"].isUInt64())
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


	const std::uint64_t sizeBytes =
		data["size_bytes"].asUInt64();

	if (sizeBytes == 0 ||
		sizeBytes > kMaxAvatarSizeBytes)
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"avatar size must be between 1 byte and 5 MiB"
			);

		connection->SendResponse(
			response.asString());

		return;
	}


	// 不用验证客户端字段名
	string clientFileId;

	const string fileName =
		data["file_name"].asString();

	// 字段名必须与 WebSocket 协议中的 content_type 完全一致。
	const string contentType =
		data["content_type"].asString();

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
			contentType,
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
					request.set_content_type(contentType);
					request.set_purpose(
						fileserver::v1::FILE_PURPOSE_AVATAR
					);

					// 同步grpc
					auto rpc_result =
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
					// 3.把上传信息存入redis
					UploadSession session;
					session.session_id =
						fileResponse.file_id();

					session.object_key =
						fileResponse.file_id();

					session.client_file_id =
						clientFileId;

					session.uploader_id =
						uploaderId;

					session.original_file_name =
						fileName;

					session.expected_content_type =
						contentType;

					session.expected_size_bytes =
						sizeBytes;

					session.purpose =
						fileserver::v1::
						FILE_PURPOSE_AVATAR;

					// 上传头像不属于某个聊天会话，因此 conversation_id 固定为 0。
					session.conversation_id = 0;

					session.expires_at_ms =
						fileResponse.expires_at_ms();

					session.upload_mode =
						UploadMode::SinglePut;

					// 第一版头像上传暂不要求客户端提交 SHA-256。
					session.expected_sha256.clear();

					// 新创建的上传会话状态必须是 pending
					session.status =
						UploadSessionStatus::Pending;

					session.created_at_ms =
						std::chrono::duration_cast<
						std::chrono::milliseconds
						>(
							std::chrono::system_clock::now().
							time_since_epoch()
						).count();

					/*
					 * RPC 业务状态成功后仍要验证必要字段。
					 * 否则 FileServer 的异常响应会被误报成 Redis 故障，
					 * 并且可能向客户端返回空的或已经过期的上传 URL。
					 */
					if (fileResponse.file_id().empty() ||
						fileResponse.upload_url().empty() ||
						session.expires_at_ms <=
						session.created_at_ms)
					{
						Json::Value response =
							BuildJsonRsp(
								"set_avatar_ack",
								requestId,
								ErrorCode::File_Request_Error,
								"FileServer returned invalid upload information"
							);

						connection->SendResponse(
							response.asString());

						return;
					}

					/*
					 * Redis 会话可以比预签名 URL 保留得更久，
					 * 便于 CompleteUpload 返回明确的过期状态并保留诊断信息。
					 * URL 是否过期仍以 expires_at_ms 为准，不能只依赖 Redis TTL。
					 */
					auto ttl = (*ConfigMgr::GetInstance())
						["FileServer"]["UploadSessionTTL"];

					ttl = ttl.empty() ? "3600" : ttl;

					// 存入redis
					auto result =
						RedisFileMgr::GetInstance()->
						CreateUploadSession(
							session,
							std::chrono::seconds(std::stoul(ttl))
						);

					// 判断redis是否成功
					if (result != RedisFileResult::Success)
					{
						std::cerr
							<< "[SetAvatarHandler] CreateUploadSession failed, result: "
							<< static_cast<int>(result)
							<< std::endl;

						Json::Value response =
							BuildJsonRsp(
								"set_avatar_ack",
								requestId,
								ErrorCode::Redis_Error,
								"Redis CreateUploadSession failed"
							);
						connection->SendResponse(
							response.asString());
						return;
					}

					// 4.把file server的上传信息转成websocket json
					Json::Value responseData{ Json::objectValue };
					responseData["file_id"] =
						fileResponse.file_id();

					// 第一版 session_id 与 file_id 相同，协议上仍显式返回。
					responseData["session_id"] =
						session.session_id;

					responseData["upload_url"] =
						fileResponse.upload_url();

					responseData["expires_at_ms"] =
						Json::Int64(fileResponse.expires_at_ms());

					Json::Value uploadHeaders
					{ Json::objectValue };

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

					connection->SendResponse(
						response.asString());

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

void SetAvatarHandler::CompleteUpload(
	const Json::Value& data,
	const string& requestId,
	std::shared_ptr<Connection> connection)
{
	if (!connection)
		return;

	std::weak_ptr<Connection> weakConn{ connection };

	if (!data.isMember("session_id") ||
		!data["session_id"].isString() ||
		data["session_id"].asString().empty())
	{
		Json::Value response =
			BuildJsonRsp(
				"set_avatar_ack",
				requestId,
				ErrorCode::Message_Json_error,
				"session_id is missing or invalid");

		connection->SendResponse(
			response.asString());

		return;
	}

	const string sessionId =
		data["session_id"].asString();

	auto uploaderId =
		connection->GetUserId();

	ThreadPool::GetInstance()->commit(
		[this,
		weakConn,
		sessionId,
		uploaderId,
		requestId]()
		{
			try
			{

				// redis查询session_id
				const auto& [result, session] =
					RedisFileMgr::GetInstance()
					->GetUploadSession(
						sessionId
					);

				// 检查redis查询状态
				switch (result)
				{
				case RedisFileResult::Success:
					break;

				case RedisFileResult::NotFound:
				{
					auto currentConn =
						weakConn.lock();

					if (!currentConn)
						return;

					Json::Value rsp =
						this->BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::
							Upload_Session_Not_Found,
							"upload session was not found"
						);

					currentConn->SendResponse(
						rsp.asString()
					);

					return;
				}

				case RedisFileResult::InvalidData:
				{
					/*
	 * Redis key 存在，但是字段缺失、数字解析失败，
	 * 或状态值不符合上传会话协议。
	 */
					std::cerr
						<< "[SetAvatarHandler] invalid upload session data"
						<< ", request_id: " 
						<< requestId
						<< ", session_id: " 
						<< sessionId
						<< std::endl;

					auto currentConnection = weakConn.lock();

					if (!currentConnection)
						return;

					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::
							Upload_Session_Invalid,
							"upload session data is invalid"
						);

					currentConnection->SendResponse(
						response.asString());

					return;
				}

				case RedisFileResult::RedisError:
				{
					/*
					 * Redis 暂时不可用属于可重试错误，
					 * 不能把上传会话永久标记为 failed。
					 */
					std::cerr
						<< "[SetAvatarHandler] Redis unavailable"
						<< ", request_id: " 
						<< requestId
						<< ", session_id: " 
						<< sessionId
						<< std::endl;

					auto currentConnection = weakConn.lock();

					if (!currentConnection)
						return;

					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::Redis_Error,
							"Redis is temporarily unavailable"
						);

					currentConnection->SendResponse(
						response.asString());

					return;
				}

				default:
				{
					/*
					 * GetUploadSession 正常情况下不会返回
					 * AlreadyExists 或 InvalidState。
					 *
					 * 出现这些值说明接口实现和调用方约定不一致，
					 * 统一作为内部 Redis 错误处理，并记录日志。
					 */
					std::cerr
						<< "[SetAvatarHandler] unexpected RedisFileResult"
						<< ", result: "
						<< static_cast<int>(result)
						<< ", request_id: " 
						<< requestId
						<< ", session_id: " 
						<< sessionId
						<< std::endl;

					auto currentConnection = weakConn.lock();

					if (!currentConnection)
						return;

					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::Redis_Error,
							"unexpected upload session result"
						);

					currentConnection->SendResponse(
						response.asString());

					return;
				}

				}

				// 检查是否有对话
				if (!session.has_value())
				{
					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::Upload_Session_Invalid,
							"redis do not return a conversation");

					auto connection = weakConn.lock();
					if (!connection)
						return;

					connection->SendResponse(
						response.asString());

					return;
				}

				auto& uploadSession =
					*session;

				if (uploaderId !=
					uploadSession.uploader_id)
				{
					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::UploaderId_Is_Mismatch,
							"current uploader_id is mismatch");

					auto connection = weakConn.lock();
					if (!connection)
						return;

					connection->SendResponse(
						response.asString());

					return;
				}

				if (sessionId !=
					session.value().session_id)
				{
					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::SessionId_Is_Mismatch,
							"the session_id do not belong this conversation");

					auto connection = weakConn.lock();
					if (!connection)
						return;

					connection->SendResponse(
						response.asString());

					return;
				}

				if (uploadSession.purpose != fileserver::v1::FILE_PURPOSE_AVATAR)
                {
                    std::cerr << "[SetAvatarHandler] upload purpose mismatch"
                              << ", request_id: " << requestId << ", session_id: " << sessionId
                              << ", purpose: " << static_cast<int>(uploadSession.purpose) << std::endl;

                    auto currentConnection = weakConn.lock();

                    if (!currentConnection)
                        return;

                    Json::Value response = BuildJsonRsp("set_avatar_ack", requestId, ErrorCode::File_Request_Error,
                                                        "upload session is not an avatar upload");

                    currentConnection->SendResponse(response.asString());

                    return;
                }

				if (uploadSession.object_key.empty())
				{
					std::cerr
						<< "[SetAvatarHandler] empty upload object key"
						<< ", request_id: "
						<< requestId
						<< ", session_id: "
						<< sessionId
						<< std::endl;

					auto currentConnection =
						weakConn.lock();

					if (!currentConnection)
						return;

					Json::Value response =
						BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::Upload_Session_Invalid,
							"upload session object key is missing"
						);

					currentConnection->SendResponse(
						response.asString());

					return;
				}

				switch (uploadSession.status)
				{
					// 已完成,直接返回
				case UploadSessionStatus::Ready:
				{
					auto currentConn =
						weakConn.lock();

					if (!currentConn)
						return;

					Json::Value rspdata{
						Json::objectValue
					};

					rspdata["session_id"] =
						uploadSession.session_id;

					rspdata["file_id"] =
						uploadSession.object_key;

					rspdata["status"] =
						"ready";

					Json::Value rsp =
						this->BuildJsonRsp(
							"set_avatar_ack",
							requestId,
							ErrorCode::Success,
							"avatar upload is already complete",
							std::move(rspdata)
						);

					currentConn->SendResponse(
						rsp.asString()
					);

					return;

				}
					// 明确失败,重新上传
				case UploadSessionStatus::Failed:
				{
					auto currentConnection =
						weakConn.lock();

					if (!currentConnection)
						return;

					Json::Value responseData{
						Json::objectValue
					};

					responseData["session_id"] =
						uploadSession.session_id;

					responseData["file_id"] =
						uploadSession.object_key;

					responseData["status"] =
						"failed";

					/*
					 * GetUploadSession 已经保证 failed 状态必须携带
					 * failure_reason，因此这里可以安全读取 value()。
					 */
					responseData["failure_reason"] =
						UploadFailureReasonToText(
							uploadSession.failure_reason.value()
						);

					/*
					 * 已失败的会话不能继续 completeUpload。
					 * 客户端需要重新申请上传地址并重新上传文件。
					 */
					responseData["recovery_action"] =
						"restart_upload";

					Json::Value response =
						BuildJsonRsp(
						"set_avatar_ack",
						requestId,
						ErrorCode::Upload_Invalid_State,
						"upload session has already failed",
						std::move(responseData)
					);

					currentConnection->SendResponse(
						response.asString()
					);

					return;
				}

				// 待确认的会话继续执行 MinIO 对象校验。
				case UploadSessionStatus::Pending:
				{
					const std::int64_t nowMs =
						std::chrono::duration_cast<
						std::chrono::milliseconds>(
							std::chrono::system_clock::now()
							.time_since_epoch()
						).count();

					/*
					 * 临时系统错误不能把上传会话改成 failed。
					 * 客户端只需要重试 completeUpload，不需要重新 PUT 文件。
					 */
					auto sendPendingRetry =
						[&](ErrorCode errorCode,
							const std::string& message)
						{
							Json::Value responseData{
								Json::objectValue
							};

							responseData["session_id"] =
								uploadSession.session_id;

							responseData["file_id"] =
								uploadSession.object_key;

							responseData["status"] =
								"pending";

							responseData["recovery_action"] =
								"retry_complete_upload";

							responseData["retry_after_ms"] =
								1000;

							responseData["expires_at_ms"] =
								Json::Int64(
									uploadSession.expires_at_ms
								);

							Json::Value response =
								BuildJsonRsp(
									"set_avatar_ack",
									requestId,
									errorCode,
									message,
									std::move(responseData)
								);

							auto currentConnection =
								weakConn.lock();

							if (!currentConnection)
								return;

							currentConnection->SendResponse(
								response.asString()
							);
						};

					/*
					 * 确定性的文件错误先原子地写入 Redis，再通知客户端
					 * 重新执行 initUpload 和 PUT。
					 */
					auto markFailedAndRespond =
						[&](UploadFailureReason reason,
							ErrorCode errorCode,
							const std::string& message)
						{
							const RedisFileResult markResult =
								RedisFileMgr::GetInstance()
								->MarkUploadFailed(
									uploadSession.session_id,
									reason,
									kUploadTerminalRetentionTtl
								);

							if (markResult !=
								RedisFileResult::Success)
							{
								std::cerr
									<< "[SetAvatarHandler] MarkUploadFailed failed"
									<< ", request_id: "
									<< requestId
									<< ", session_id: "
									<< uploadSession.session_id
									<< ", redis_result: "
									<< static_cast<int>(markResult)
									<< std::endl;

								/*
								 * Redis 状态没有可靠地更新时，不能告诉客户端
								 * 会话已经进入 failed，保留 pending 语义等待重试。
								 */
								sendPendingRetry(
									ErrorCode::Redis_Error,
									"failed to update upload session"
								);

								return;
							}

							Json::Value responseData{
								Json::objectValue
							};

							responseData["session_id"] =
								uploadSession.session_id;

							responseData["file_id"] =
								uploadSession.object_key;

							responseData["status"] =
								"failed";

							responseData["failure_reason"] =
								UploadFailureReasonToText(reason);

							responseData["recovery_action"] =
								"restart_upload";

							Json::Value response =
								BuildJsonRsp(
									"set_avatar_ack",
									requestId,
									errorCode,
									message,
									std::move(responseData)
								);

							auto currentConnection =
								weakConn.lock();

							if (!currentConnection)
								return;

							currentConnection->SendResponse(
								response.asString()
							);
						};

					if (nowMs >= uploadSession.expires_at_ms)
					{
						markFailedAndRespond(
							UploadFailureReason::UploadExpired,
							ErrorCode::Upload_Session_Expired,
							"upload session has expired"
						);

						return;
					}

					// 构造grpc通信使用的completeReq
					fileserver::v1::CompleteUploadReq
						request;

					request.set_file_id(
						uploadSession.object_key);

					request.set_uploader_id(
						uploadSession.uploader_id);

					request.set_request_id(
						requestId);

					auto result = FileGrpcClient::GetInstance()
						->CompUpload(request);

					if (!result.TransportOk())
					{
						/*
						 * gRPC 传输失败只表示 ChatServer 暂时无法确认 MinIO 对象。
						 * 文件可能已经成功上传，因此不能把 Redis 会话标记为 failed，
						 * 也不能要求客户端重新 PUT 文件。
						 */
						std::cerr
							<< "[SetAvatarHandler] CompleteUpload RPC failed"
							<< ", request_id: "
							<< requestId
							<< ", session_id: "
							<< uploadSession.session_id
							<< ", grpc_code: "
							<< result.grpc_status.error_code()
							<< ", grpc_message: "
							<< result.grpc_status.error_message()
							<< std::endl;

						sendPendingRetry(
							ErrorCode::File_Rpc_Error,
							"FileServer is temporarily unavailable"
						);

						return;
					}

					const auto& fileResponse =
						result.response;

					// FileServer 返回不完整响应。
					if (!fileResponse.has_result())
					{
						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"FileServer returned an incomplete response"
						);

						return;
					}

					const auto& fileResult =
						fileResponse.result();

					switch (fileResult.code())
					{
					case fileserver::v1::FILE_RESULT_OK:
						break;

					case fileserver::v1::FILE_RESULT_NOT_FOUND:
						markFailedAndRespond(
							UploadFailureReason::ObjectNotFound,
							ErrorCode::Upload_Metadata_Mismatch,
							"uploaded object was not found"
						);

						return;

					case fileserver::v1::FILE_RESULT_SIZE_MISMATCH:
						markFailedAndRespond(
							UploadFailureReason::SizeMismatch,
							ErrorCode::Upload_Metadata_Mismatch,
							"uploaded file size does not match"
						);

						return;

					case fileserver::v1::FILE_RESULT_UPLOAD_EXPIRED:
						markFailedAndRespond(
							UploadFailureReason::UploadExpired,
							ErrorCode::Upload_Session_Expired,
							"upload session has expired"
						);

						return;

					case fileserver::v1::FILE_RESULT_INTERNAL_ERROR:
						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"file storage is temporarily unavailable"
						);

						return;

					case fileserver::v1::FILE_RESULT_INVALID_REQUEST:
					case fileserver::v1::FILE_RESULT_UNAUTHORIZED:
					default:
						std::cerr
							<< "[SetAvatarHandler] unexpected FileServer result"
							<< ", request_id: "
							<< requestId
							<< ", session_id: "
							<< uploadSession.session_id
							<< ", file_result: "
							<< static_cast<int>(fileResult.code())
							<< ", message: "
							<< fileResult.message()
							<< std::endl;

						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"FileServer rejected the upload confirmation"
						);

						return;
					}

					/*
					 * FileServer 表示成功时必须同时返回 MinIO 实际元数据。
					 */
					if (!fileResponse.has_file())
					{
						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"FileServer did not return file metadata"
						);

						return;
					}

					const auto& actualFile =
						fileResponse.file();

					/*
					 * file_id 和 uploader_id 用于检查 ChatServer 与 FileServer
					 * 的请求/响应是否对应同一个上传会话。
					 */
					if (actualFile.file_id() !=
						uploadSession.object_key)
					{
						std::cerr
							<< "[SetAvatarHandler] FileServer file_id mismatch"
							<< ", request_id: "
							<< requestId
							<< ", expected: "
							<< uploadSession.object_key
							<< ", actual: "
							<< actualFile.file_id()
							<< std::endl;

						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"FileServer returned mismatched file identity"
						);

						return;
					}

					if (actualFile.uploader_id() !=
						uploadSession.uploader_id)
					{
						std::cerr
							<< "[SetAvatarHandler] FileServer uploader mismatch"
							<< ", request_id: "
							<< requestId
							<< ", expected: "
							<< uploadSession.uploader_id
							<< ", actual: "
							<< actualFile.uploader_id()
							<< std::endl;

						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"FileServer returned mismatched uploader"
						);

						return;
					}

					if (actualFile.status() !=
						fileserver::v1::FILE_STATUS_READY)
					{
						sendPendingRetry(
							ErrorCode::File_Request_Error,
							"uploaded object is not ready"
						);

						return;
					}

					/*
					 * size_bytes 和 content_type 来自 MinIO StatObject，
					 * 必须与 initUpload 时保存在 Redis 的预期值完全一致。
					 */
					if (actualFile.size_bytes() !=
						uploadSession.expected_size_bytes)
					{
						std::cerr
							<< "[SetAvatarHandler] uploaded size mismatch"
							<< ", request_id: "
							<< requestId
							<< ", expected: "
							<< uploadSession.expected_size_bytes
							<< ", actual: "
							<< actualFile.size_bytes()
							<< std::endl;

						markFailedAndRespond(
							UploadFailureReason::SizeMismatch,
							ErrorCode::Upload_Metadata_Mismatch,
							"uploaded file size does not match"
						);

						return;
					}

					if (actualFile.content_type() !=
						uploadSession.expected_content_type)
					{
						std::cerr
							<< "[SetAvatarHandler] uploaded content type mismatch"
							<< ", request_id: "
							<< requestId
							<< ", expected: "
							<< uploadSession.expected_content_type
							<< ", actual: "
							<< actualFile.content_type()
							<< std::endl;

						markFailedAndRespond(
							UploadFailureReason::ContentTypeMismatch,
							ErrorCode::Upload_Metadata_Mismatch,
							"uploaded file content type does not match"
						);

						return;
					}

					std::cout
						<< "[SetAvatarHandler] uploaded object metadata verified"
						<< ", request_id: "
						<< requestId
						<< ", session_id: "
						<< uploadSession.session_id
						<< ", file_id: "
						<< actualFile.file_id()
						<< std::endl;

					/*
					 * 到这里说明 Redis 预期元数据与 MinIO 实际元数据一致。
					 * 下一步必须先幂等更新 MySQL 的 avatar_file_id 和
					 * profile_version，成功后才能调用 MarkUploadReady。
					 */

				}

				break;
				}

			}
			catch (...)
			{

			}

		});

}
