#pragma once
#include "const.h"

class Server :public std::enable_shared_from_this<Server>
{
public:
	Server(boost::asio::io_context& ioc,unsigned short port) : acceptor_(ioc, tcp::endpoint(tcp::v4(), port)), socket_(ioc),ioc_(ioc){}
	
	void start();
private:
	boost::asio::io_context& ioc_;
	tcp::acceptor acceptor_;
	tcp::socket socket_;
};

