#pragma once
#include "const.h"

namespace asio = boost::asio;
namespace beast = boost::beast;

struct UserInfo
{
	int uuid;		// 用户id
	string token; // 登录状态效验
	string Request_type;	//请求类型:solo chat/group chat...,
	string data;// data可以是json格式

};


class Connection : public std::enable_shared_from_this<Connection>
{
public:
	Connection(asio::io_context& ioc);
	string GetId();
	asio::ip::tcp::socket& GetSocket();
	void Start();

	// websocket握手升级
	void AsyncAccept();

	// 发给客户端
	void SendResponse(std::string send_json);

	// 构造发送包
	std::string
		JsonResponse(
			ErrorCode error,
			const std::string& message,
			const std::string& email = "",
			const std::string& token = "",
			const std::string& host = "",
			const std::string& port = ""
		)
	{
		Json::Value root;
		root[ "error" ] = static_cast<int>(error);
		root[ "message" ] = message;
		if ( !email.empty() )
			root[ "email" ] = email;
		if ( !token.empty() )
			root[ "token" ] = token;
		if ( !host.empty() )
			root[ "host" ] = host;
		if ( !port.empty() )
			root[ "port" ] = port;

		//让字节变得紧凑
		Json::StreamWriterBuilder writer;
		writer[ "indentation" ] = "";

		return Json::writeString(writer, root);
	}
private:
	void AsyncWrite();

	unique_ptr<beast::websocket::stream<beast::tcp_stream>> ws_; // 管理 WebSocket 连接的智能指针
	string uuid_; // 每个连接唯一的uuid
	asio::io_context& ioc_;
	beast::flat_buffer recv_buff_; // 接收缓冲区
	std::queue<string> send_queue_;

	string token_;
};

