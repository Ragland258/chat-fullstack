#pragma once


#include "const.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

class LogicSystem;

class HttpConnection
    : public std::enable_shared_from_this<HttpConnection>
{
	friend class LogicSystem;

public:
    explicit HttpConnection(tcp::socket&& socket);

    explicit HttpConnection(
        boost::asio::io_context& ioc
    );

	void Start();

    tcp::socket& GetSocket()
    {
        return socket_;
    }

    http::response<http::dynamic_body> GetResponse() const
    {
        return response_;
    }

    /**
     * 获取 HTTP 请求体。
     *
     * 返回字符串副本，因此 Handler 可以安全地将请求体
     * 移动到线程池任务中。
     */
    std::string GetRequest() const
    {
        return boost::beast::buffers_to_string(
            request_.body().data()
        );
    }

    /**
     * 发送 JSON 响应。
     *
     * 该函数可以从业务线程调用。
     * 实际 response_ 修改和网络写入会被投递回
     * socket 对应的 Asio executor。
     */
	void SendJsonResponse(
		http::status status,
        std::string jsonBody
    );


private:
    /**
     * 启动连接超时检查。
     */
    void CheckDeadline();

    /**
     * 发送当前 response_。
     *
     * responseStarted 为 true 表示调用方已经设置过
     * response_started_，不再重复检查。
     */
    void WriteResponse(
        bool responseStarted = false
    );

    /**
     * 分发当前 HTTP 请求。
     */
    void HandleRequest();

    /**
     * 根据 Host 请求头和 target 构造完整 URL，
     * 仅用于日志输出。
     */
    std::string GetFullUrl() const;

private:
    tcp::socket socket_;

    beast::flat_buffer buffer_;

    http::request<http::dynamic_body>
        request_;

    http::response<http::dynamic_body>
        response_;

    /**
     * HTTP 请求处理超时时间。
     */
    boost::asio::steady_timer deadline_{
        socket_.get_executor(),
        std::chrono::seconds(30)
};

    /**
     * 防止同一个请求重复发送响应。
     */
    std::atomic_bool response_started_{ false };

    std::atomic_bool timed_out_{ false };
};