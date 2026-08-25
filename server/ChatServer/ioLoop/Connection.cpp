#include "Connection.h"
#include "ConnectionMgr.h"
#include "route/LogicRoute.h"
#include "ThreadPool.h"
#include "ConfigMgr.h"
#include "redis/RedisMgr.h"
namespace
{
	/**
	 * 将 Beast string_view 转成拥有内存的 std::string。
	 */
	std::string BeastViewToString(
		boost::beast::string_view view)
	{
		return std::string(
			view.data(),
			view.size()
		);
	}

	/**
	 * 从 HTTP target 中提取路由路径。
	 *
	 * 例如：
	 *
	 * /get_verify
	 *      -> /get_verify
	 *
	 * /get_verify?source=web
	 *      -> /get_verify
	 */
	std::string ExtractRoutePath(
		const std::string& target)
	{
		const auto queryPosition =
			target.find('?');

		if ( queryPosition == std::string::npos )
		{
			return target;
		}

		return target.substr(
			0,
			queryPosition
		);
	}

	inline void printDispatch(const std::string& postUrl, const std::string& fullUrl)
	{
		std::cout << "[dispatch] POST url: "
			<< postUrl
			<< ", full url: "
			<< fullUrl
			<< std::endl;
	}

	inline void printRequest(const std::string& method, const std::string& rawTarget, const std::string& fullUrl)
	{
		std::cout
			<< "[request] method: "
			<< method
			<< ", target: "
			<< rawTarget
			<< ", full url: "
			<< fullUrl
			<< std::endl;
	}
}


Connection::Connection(asio::io_context& ioc) 
	: ioc_(ioc), ws_(std::make_unique < beast::websocket::stream<beast::tcp_stream>>(asio::make_strand(ioc)))
{
	// 生成唯一uuid
	boost::uuids::random_generator generator;
	boost::uuids::uuid uuid = generator();
	session_id_ = boost::uuids::to_string(uuid);
}

string Connection::GetId()
{
	return session_id_;
}

asio::ip::tcp::socket& Connection::GetSocket()
{
	return beast::get_lowest_layer(*ws_).socket();
}

void Connection::SendResponse(std::string send_json)
{
	auto self = shared_from_this();
	asio::post(
		ws_->get_executor(),
		[self, message = std::move(send_json)]() mutable
		{
			const bool write_in_progress = !self->send_queue_.empty();
			self->send_queue_.push(std::move(message));
			if (!write_in_progress)
			{
				self->AsyncWrite();
			}
		}
	);
}

std::string Connection::JsonResponse(ErrorCode error, const std::string& message, Json::Value data)
{
	Json::Value root(Json::objectValue);

	root[ "error" ] =
		static_cast<int>(error);

	root[ "message" ] =
		message;

	if ( !data.isNull() )
	{
		root[ "data" ] =
			std::move(data);
	}

	return WriteJson(root);
}

void Connection::AsyncWrite()
{
	auto self = shared_from_this();
	ws_->text(true);
	ws_->async_write(
		asio::buffer(send_queue_.front()),
		[self](boost::system::error_code error, std::size_t)
		{
			if (error)
			{
				std::cerr << "websocket async write error is " << error.what() << std::endl;
				ConnectionMgr::GetInstance()->RmvConnection(self->GetId());
				return;
			}

			self->send_queue_.pop();
			if (!self->send_queue_.empty())
			{
				self->AsyncWrite();
			}
		}
	);
}

void Connection::BindSession(AuthSession session)
{
	user_ = std::move(session);
}

std::uint64_t Connection::ParseUint64(beast::string_view text)
{
	if ( text.empty() )
		return 0;

	std::uint64_t value{ 0 };

	const char* begin = text.data();
	const char* end = begin + text.size();

	auto [position, error] =
		std::from_chars(begin, end, value);

	if ( error != std::errc{} ||
		position != end ||
		value == 0 )
	{
		return 0;
	}

	return value;
}

std::string Connection::ParseBearer(beast::string_view authorization)
{
	constexpr beast::string_view prefix = "Bearer ";

	if ( authorization.size() <= prefix.size() )
		return { };
	if ( authorization.substr(0, prefix.size()) != prefix )
		return { };

	const auto token = authorization.substr(prefix.size());
	return std::string(token.data(), token.size());
}

void Connection::RejectHandShake(
	http::status status,
	std::string message)
{
	auto self = shared_from_this();
	auto response = std::make_shared<http::response<http::string_body>>(
		status,
		handshake_request_.version());

	response->set(http::field::content_type, "text/plain; charset=utf-8");
	response->set(http::field::connection, "close");
	response->body() = std::move(message);
	response->prepare_payload();

	http::async_write(
		ws_->next_layer(),
		*response,
		[self, response](boost::system::error_code, std::size_t)
		{
			boost::system::error_code ignored;
			beast::get_lowest_layer(*self->ws_).socket().shutdown(
				asio::ip::tcp::socket::shutdown_both,
				ignored);
			beast::get_lowest_layer(*self->ws_).socket().close(ignored);
		});
}

void Connection::AcceptUpgrade()
{
	auto self = shared_from_this();

	ws_->async_accept(
		handshake_request_,
		[ self ] (boost::system::error_code ec)
		{
			if ( ec )
			{
				std::cout << "websocket upgrade failed: "
					<< ec.message() << std::endl;
				return;
			}

			// 现在已经完成鉴权和WebSocket升级
			ConnectionMgr::GetInstance()->AddConnection(self);
			self->Start();
		});
}

std::string Connection::WriteJson(const Json::Value& root)
{
	Json::StreamWriterBuilder writer;

	// 关闭缩进和换行，压缩 JSON。
	writer[ "indentation" ] = "";

	return Json::writeString(
		writer,
		root);
}

void Connection::Start()
{
	auto self = shared_from_this();
	
	// connection异步读
	ws_->async_read(
		recv_buff_,
		[self](boost::system::error_code err,std::size_t trans_byte)
		{
				try
				{
					if ( err )
					{
						std::cout << "websocket async read error is " << err.what() << std::endl;
						ConnectionMgr::GetInstance()->RmvConnection(self->GetId());
						return;
					}
					self->ws_->text(self->ws_->got_text()); // got_text()获取对应消息格式类型,一共有两种
					std::string recv_data = boost::beast::buffers_to_string(self->recv_buff_.data());// 把消息转化为字节形式
					self->recv_buff_.consume(self->recv_buff_.size());// 清空缓冲区
					std::cout << "websocket receive msg is " << recv_data << std::endl;

					/*
					* LogicSystem 只负责寻找并调用 Handler。
					*	
					* Handler 负责：
					*
					* 1. 解析请求;
					* 2. 执行业务；
					* 3. 调用 SendJsonResponse 返回最终结果。
					*/
					LogicRoute::GetInstance()->HandlerPost(
						std::move(recv_data), 
						self
					);

					// 递归继续监听
					self->Start();
				}
				catch ( std::exception& exp )
				{
					std::cout << "exception is " << exp.what() << std::endl;
					ConnectionMgr::GetInstance()->RmvConnection(self->GetId());
				}
		});
}

bool Connection::IsAuthenticated() const noexcept
{
	return user_.has_value();
}

std::uint64_t Connection::GetUserId() const noexcept
{
	return user_ ? user_->uid : 0;
}

std::uint64_t Connection::GetDeviceId() const noexcept
{
	return user_ ? user_->device_id : 0;
}

std::string Connection::GetEmail() const
{
	return user_ ? user_->email : std::string{};
}

void Connection::AsyncAccept()
{
	auto self = shared_from_this();
	http::async_read(
		ws_->next_layer(),
		handshake_buffer_,
		handshake_request_,
		[self](boost::system::error_code err, std::size_t)
		{
			if ( err )
			{
				std::cout << "read upgrade request failed: "
					<< err.message()
					<< std::endl;
				return;
			}

			const auto& request = self->handshake_request_;

			const auto authorization =
				request[ http::field::authorization ];
			const auto uidHeader =
				request[ "X-User-Id" ];
			const auto deviceHeader =
				request[ "X-Device-Id" ];

			if ( authorization.empty() ||
				uidHeader.empty() ||
				deviceHeader.empty() )
			{
				self->RejectHandShake(
					http::status::unauthorized,
					"Missing authentication headers"
				);
				return;
			}

			// Authorization: Bearer <token>
			// Authorization: Bearer xxxxxxxxxx
			std::string token =
				self->ParseBearer(authorization);

			const std::uint64_t uid =
				self->ParseUint64(uidHeader);
			const std::uint64_t deviceId =
				self->ParseUint64(deviceHeader);

			if ( token.empty() || uid == 0 || deviceId == 0 )
			{
				self->RejectHandShake(
					http::status::unauthorized,
					"Missing authentication headers"
				);
				return;
			}

			const auto serverId =
				(*(ConfigMgr::GetInstance()))[ "ChatServer" ][ "ServerId" ];

			if ( serverId.empty() )
			{
				self->RejectHandShake(
					http::status::internal_server_error,
					"ChatServer ServerId is not configured"
				);
				return;
			}

			/*
			 * Redis 查询会阻塞，因此投递到工作线程，避免阻塞
			 * WebSocket 所在的 I/O 线程。
			 *
			 * 这里必须让任务持有 shared_ptr self：当前连接尚未完成
			 * WebSocket Upgrade，也还没有加入 ConnectionMgr。若只捕获
			 * weak_ptr，读取回调结束后 Connection 可能立即析构，客户端
			 * 就只能看到 socket hang up。
			 */
			ThreadPool::GetInstance()->commit(
				[self,
				uid,
				deviceId,
				token = std::move(token),
				serverId]() mutable
				{
					try
					{
						auto loginSession =
							RedisMgr::GetInstance()->VerifyLoginSession
						(
							uid,
							deviceId,
							token,
							serverId
						);

						/*
						 * Redis 查询运行在线程池中；认证状态和 WebSocket
						 * 操作仍然回到该连接的 executor 上串行执行。
						 */
						asio::post(
							self->ws_->get_executor(),
							[self, loginSession = std::move(loginSession)]() mutable
							{
								if ( !loginSession )
								{
									self->RejectHandShake(
										http::status::unauthorized,
										"Authentication failed"
									);
									return;
								}

								AuthSession session;
								session.device_id = loginSession->device_id;
								session.uid = loginSession->uid;
								session.email = std::move(loginSession->email);
								self->BindSession(std::move(session));

								self->AcceptUpgrade();
							}
						);
					}
					catch ( const std::exception& exception )
					{
						/*
						 * commit() 返回的 future 在这里没有被保存，异常不会
						 * 自动传播到 I/O 线程，因此必须在任务内部处理。
						 */
						std::cerr
							<< "[WebSocket authentication] exception: "
							<< exception.what()
							<< std::endl;

						asio::post(
							self->ws_->get_executor(),
							[self]()
							{
								self->RejectHandShake(
									http::status::internal_server_error,
									"Authentication service error"
								);
							}
						);
					}
				}
			);
		});
}
