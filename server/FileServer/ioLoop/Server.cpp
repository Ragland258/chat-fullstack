#include "Server.h"
#include "IOSPool.h"

Server::Server(asio::io_context& ioc, unsigned short port)
	:ioc_(ioc), acceptor_(ioc,asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
{}
