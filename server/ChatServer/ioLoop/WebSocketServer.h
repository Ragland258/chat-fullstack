#pragma once
#include "const.h"

class WebSocketServer
{
public:
	WebSocketServer(const WebSocketServer&) = delete;
	WebSocketServer& operator = (const WebSocketServer&) = delete;

	WebSocketServer(asio::io_context& ioc, unsigned short port);
	void StartAccept();
private:
	asio::io_context& ioc_;
	asio::ip::tcp::acceptor acceptor_;
};

