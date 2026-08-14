#include <iostream>
#include "ioLoop/WebSocketServer.h"
#include "const.h"
#include "IOSPool.h"
int main()
{
	try
	{
		asio::io_context ioc;
		asio::signal_set signals(ioc, SIGINT, SIGTERM);

		signals.async_wait(
			[&](const boost::system::error_code& ec, int)
			{
				if (ec)
					return;

				IOSPool::GetInstance()->Stop();
				ioc.stop();
			}
		);
		unsigned int port = 7891;
		auto server = std::make_shared<WebSocketServer>(ioc, port);
		server->StartAccept();
		std::cout << "Server is running on port " << port << std::endl;
		ioc.run();
	}
	catch(std::exception& e)
	{
		std::cerr << "Exception:" << e.what() << std::endl;
	}
}
