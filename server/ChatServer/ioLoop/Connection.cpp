#include "Connection.h"
#include "ConnectionMgr.h"
#include "route/LogicRoute.h"

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
	uuid_ = boost::uuids::to_string(uuid);
}

string Connection::GetId()
{
	return uuid_;
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

void Connection::AsyncAccept()
{
	auto self = shared_from_this();
	ws_->async_accept(
		[self](boost::system::error_code err)
		{
			try
			{
				if (!err)
				{
					ConnectionMgr::GetInstance()->AddConnection(self);
					self->Start();
				}
				else
				{
					std::cout << "websocket accept failed, err is " << err.what() << std::endl;
				}
			}
			catch (std::exception& ec)
			{
				auto err_msg = ec.what();
				std::cout << "websocket async accept exception :" << err_msg << std::endl;;
			}
		});
}
