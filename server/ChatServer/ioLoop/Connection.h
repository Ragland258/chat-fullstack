#pragma once
#include "const.h"

namespace asio = boost::asio;
namespace beast = boost::beast;


struct AuthSession
{
	std::uint64_t uid{ 0 };
	std::uint64_t device_id{ 0 };
	string email;
};

class Connection : public std::enable_shared_from_this<Connection>
{
public:
	Connection(asio::io_context& ioc);
	string GetId();
	asio::ip::tcp::socket& GetSocket();
	void Start();

	bool IsAuthenticated() const noexcept;
	std::uint64_t GetUserId() const noexcept;
	std::uint64_t GetDeviceId() const noexcept;
	std::string GetEmail() const;


	// websocket握手升级 + 处理包头
	void AsyncAccept();

	// 发给客户端
	void SendResponse(std::string send_json);

	// 普通响应包
	std::string JsonResponse(
		ErrorCode error,
		const std::string& message,
		Json::Value data = Json::Value()
	);

	/**
	 * @brief 构造一个带 ACK 的 JSON 响应包。
	 *
	 * ACK 用于告诉客户端：
	 *
	 * 1. 服务器是否成功处理了请求；
	 * 2. 消息是否已经保存；
	 * 3. 本次请求是否为重复请求；
	 * 4. 请求失败的具体原因。
	 *
	 * 最终格式：
	 *
	 * {
	 *     "version": 1,
	 *     "type": "send_message_ack",
	 *     "request_id": "request-001",
	 *     "ack": {
	 *         "code": 0,
	 *         "message": "message stored"
	 *     },
	 *     "data": {
	 *         ...
	 *     }
	 * }
	 *
	 * @tparam AckCode
	 *     ACK 枚举类型。
	 *
	 *     可以是：
	 *
	 *     SendAckCode
	 *     ReadAckCode
	 *     AuthAckCode
	 *
	 *     但必须是 enum 或 enum class 类型。
	 *
	 * @param type
	 *     响应消息类型。
	 *
	 *     例如：
	 *
	 *     send_message_ack
	 *     read_receipt_ack
	 *     auth_ack
	 *
	 * @param request_id
	 *     客户端请求 ID。
	 *
	 *     用于让客户端知道这个 ACK 对应哪一次请求。
	 *     例如客户端发送 request-001，服务器返回同一个 request-001。
	 *
	 * @param ack_code
	 *     ACK 状态码。
	 *
	 *     例如：
	 *
	 *     SendAckCode::Stored
	 *     SendAckCode::Duplicate
	 *     SendAckCode::Unauthorized
	 *
	 * @param message
	 *     ACK 的文字说明，主要用于日志和调试。
	 *
	 * @param data
	 *     具体业务数据。
	 *
	 *     例如消息 ACK 中可以放：
	 *
	 *     message_id
	 *     conversation_id
	 *     conversation_seq
	 *     server_time_ms
	 *
	 *     如果没有额外数据，可以不传。
	 */
	template<typename AckCode>
	std::string JsonAckResponse(
		const std::string& type,
		const std::string& request_id,
		AckCode ack_code,
		const std::string& message,
		Json::Value data = Json::Value()
	);

private:
	void AsyncWrite();
	void BindSession(AuthSession session);

	void RejectHandShake(
		http::status status,
		std::string message);

	// 用于转换包头中的uid和设备id
	std::uint64_t ParseUint64(beast::string_view text);

	// 从 Authorization: Bearer <token> 中提取 token。
	std::string ParseBearer(beast::string_view authorization);

	// 用于完成真正的升级
	void AcceptUpgrade();

	static std::string WriteJson(
		const Json::Value& root);
private:

	// 升级

	beast::flat_buffer handshake_buffer_;
	http::request<http::string_body> handshake_request_;

	unique_ptr<beast::websocket::stream<beast::tcp_stream>> ws_; // 管理 WebSocket 连接的智能指针
	string session_id_; // 每个连接唯一的uuid
	asio::io_context& ioc_;
	beast::flat_buffer recv_buff_; // 接收缓冲区
	std::queue<string> send_queue_;
	std::optional<AuthSession> user_;		// 连接存储的用户信息
};

template<typename AckCode>
inline std::string Connection::JsonAckResponse(
	const std::string& type, 
	const std::string& request_id, 
	AckCode ack_code, 
	const std::string& message, 
	Json::Value data)
{

	static_assert(
		std::is_enum_v<AckCode>,
		"AckCode must be an enum type");

	/*
	 * 创建最外层 JSON 对象。
	 *
	 * Json::objectValue 表示：
	 *
	 * {
	 * }
	 *
	 * 而不是数组：
	 *
	 * [
	 * ]
	 */
	Json::Value root(Json::objectValue);

	/*
	 * 协议版本。
	 *
	 * 以后修改消息格式时，可以增加到 version=2，
	 * 客户端根据版本决定怎样解析。
	 */
	root[ "version" ] = 1;

	/*
	 * 响应类型。
	 *
	 * 客户端通过 type 决定把消息交给哪个处理器。
	 *
	 * 例如：
	 *
	 * send_message_ack
	 *     → 发送消息 ACK 处理器
	 *
	 * read_receipt_ack
	 *     → 已读回执 ACK 处理器
	 */
	root[ "type" ] = type;

	/*
	 * request_id 用于匹配请求和响应。
	 *
	 * 客户端发送：
	 *
	 * request_id = request-001
	 *
	 * 服务器应该返回：
	 *
	 * request_id = request-001
	 *
	 * 如果服务器主动推送消息，没有对应请求，
	 * request_id 可以为空。
	 */
	if ( !request_id.empty() )
	{
		root[ "request_id" ] =
			request_id;
	}

	/*
	 * 创建 ACK 对象。
	 *
	 * 最后将形成：
	 *
	 * "ack": {
	 *     "code": 0,
	 *     "message": "message stored"
	 * }
	 */
	Json::Value ack(Json::objectValue);

	/*
	 * enum class 不能直接存入 Json::Value，
	 * 所以需要转换成整数。
	 *
	 * 例如：
	 *
	 * SendAckCode::Stored
	 *     → 0
	 *
	 * SendAckCode::Duplicate
	 *     → 1
	 *
	 * SendAckCode::Unauthorized
	 *     → 1001
	 */
	ack[ "code" ] =
		static_cast<int>(ack_code);

	/*
	 * ACK 的文字说明。
	 *
	 * code 适合程序判断，
	 * message 适合人查看日志和调试。
	 *
	 * 客户端的核心逻辑应该判断 code，
	 * 不要依赖 message 字符串。
	 */
	ack[ "message" ] =
		message;

	/*
	 * 把 ACK 对象放进最外层 JSON。
	 *
	 * std::move 表示把 ack 内部数据转移给 root，
	 * 避免额外复制。
	 *
	 * 执行后形成：
	 *
	 * {
	 *     "ack": {
	 *         ...
	 *     }
	 * }
	 */
	root[ "ack" ] =
		std::move(ack);

	/*
	 * data 保存具体业务数据。
	 *
	 * 如果调用者没有传 data，
	 * data 是 Json null，不会写入响应。
	 *
	 * 如果传入了 data，则生成：
	 *
	 * "data": {
	 *     "message_id": "...",
	 *     "conversation_seq": 105
	 * }
	 */
	if ( !data.isNull() )
	{
		/*
		 * data 是按值传入的局部变量，
		 * 所以这里可以安全地 std::move。
		 */
		root[ "data" ] =
			std::move(data);
	}

	/*
	 * 把 Json::Value 转换成 std::string，
	 * 这样才能通过 WebSocket 发送。
	 */
	return WriteJson(root);
}
