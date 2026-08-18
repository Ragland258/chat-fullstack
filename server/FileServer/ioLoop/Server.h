#pragma once

#include "const.h"
class Server : public std::enable_shared_from_this<Server>
{
public:
	Server(asio::io_context& ioc, unsigned short port);

private:
	asio::io_context& ioc_;
	asio::ip::tcp::acceptor acceptor_;

};

