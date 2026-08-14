#include "Server.h"
#include "HttpConnection.h"
#include "IOSPool.h"


void Server::start()
{
	auto self = shared_from_this();
	auto& io_context = IOSPool::GetInstance()->GetIOService();
	auto new_connection = std::make_shared<HttpConnection>(io_context);

	acceptor_.async_accept(new_connection->GetSocket(), [self, new_connection](beast::error_code ec)
		{
			try
			{
				// 主动关闭acceptor，直接退出，不再继续监听
				if (ec == boost::asio::error::operation_aborted)
				{
					return;
				}

				if (ec)
				{
					std::cout << "accept error:" << ec.what() << std::endl;
					self->start();
					return;
				}

				new_connection->Start();
			}
			catch (std::exception& ex)
			{
				std::cout << "accept callback exception:" << ex.what() << std::endl;
			}

			// 无论成功/异常，只要不是operation_aborted，继续监听
			if (ec != boost::asio::error::operation_aborted)
			{
				self->start();
			}
		});
}