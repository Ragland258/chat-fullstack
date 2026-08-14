#include "WebSocketServer.h"
#include "IOSPool.h"
#include "Connection.h"
WebSocketServer::WebSocketServer(asio::io_context& ioc, unsigned short port)
	:ioc_(ioc),acceptor_(ioc,asio::ip::tcp::endpoint(asio::ip::tcp::v4(),port))
{
}

void WebSocketServer::StartAccept()
{
	auto& ioc = IOSPool::GetInstance()->GetIOService();
	auto connection = std::make_shared<Connection>(ioc);

	acceptor_.async_accept(connection->GetSocket(),
		[this, connection](boost::system::error_code err)
		{
			try
			{
				if (!err)
				{
					connection->AsyncAccept();
				}
				else
				{
					std::cout << "acceptor async_accept failed, err is " << err.what() << std::endl;
					// 主动关闭acceptor，停止循环
					if (err == boost::asio::error::operation_aborted)
					{
						return;
					}
				}
			}
			catch (std::exception& exp)
			{
				std::cout << "async_accept error is " << exp.what() << std::endl;
			}

			// 把StartAccept挪到try外面，无论是否异常，只要不是operation_aborted都继续监听
			if (!err || err != boost::asio::error::operation_aborted)
			{
				StartAccept();
			}
		});
}
