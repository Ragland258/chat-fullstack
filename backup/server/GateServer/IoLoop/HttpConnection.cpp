#include "HttpConnection.h"

#include "Work/WorkRoute/LogicSystem.h"

#include <boost/asio/post.hpp>
#include <boost/beast/core/ostream.hpp>
#include <boost/core/ignore_unused.hpp>

#include <iostream>
#include <utility>

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

        if (queryPosition == std::string::npos)
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

HttpConnection::HttpConnection(
    tcp::socket&& socket)
    : socket_(std::move(socket))
{
}

HttpConnection::HttpConnection(
    boost::asio::io_context& ioc)
    : socket_(ioc)
{
}

std::string HttpConnection::GetFullUrl() const
{
    /*
     * request_.target() 一般只包含：
     *
     * /get_verify
     *
     * 或：
     *
     * /get_verify?source=web
     */
    const std::string target =
        BeastViewToString(
            request_.target()
        );

    std::string host;

    const auto hostIterator =
        request_.base().find(
            http::field::host
        );

    if (hostIterator != request_.base().end())
    {
        host = BeastViewToString(
            request_[http::field::host]
        );
    }

    if (host.empty())
    {
        host = "unknown-host";
    }

    return "http://" + host + target;
}
void HttpConnection::Start()
{
    auto self = shared_from_this();

    // 从真正开始读取请求时重新计算超时时间
    deadline_.expires_after(
        std::chrono::seconds(30)
    );

    CheckDeadline();

    http::async_read(
        socket_,
        buffer_,
        request_,
        [self](
            boost::beast::error_code error,
            std::size_t bytesTransferred)
        {
            try
            {
                if (error)
                {
                    /*
                     * deadline 主动关闭 socket 后，
                     * async_read 会返回连接中止错误。
                     * 这是预期行为，不重复打印错误。
                     */
                    if (self->timed_out_.load())
                    {
                        return;
                    }

                    /*
                     * 客户端正常关闭连接。
                     */
                    if (error == http::error::end_of_stream)
                    {
                        self->deadline_.cancel();
                        return;
                    }

                    /*
                     * 其他操作取消。
                     */
                    if (error
                        == boost::asio::error::operation_aborted)
                    {
                        return;
                    }

                    std::cerr
                        << "[http] read error: "
                        << error.message()
                        << std::endl;

                    self->deadline_.cancel();
                    return;
                }

                boost::ignore_unused(
                    bytesTransferred
                );

                self->HandleRequest();
            }
            catch (const std::exception& exception)
            {
                std::cerr
                    << "[http] request exception: "
                    << exception.what()
                    << std::endl;

                self->SendJsonResponse(
                    http::status::internal_server_error,
                    R"({"error":500,"message":"internal server error"})"
                );
            }
            catch (...)
            {
                std::cerr
                    << "[http] unknown request exception"
                    << std::endl;

                self->SendJsonResponse(
                    http::status::internal_server_error,
                    R"({"error":500,"message":"internal server error"})"
                );
            }
        }
    );
}

void HttpConnection::CheckDeadline()
{
    auto self = shared_from_this();

    deadline_.async_wait(
        [self](boost::beast::error_code error)
        {
            try
            {
                /*
                 * 请求已经完成，定时器被取消。
                 */
                if (error
                    == boost::asio::error::operation_aborted)
                {
                    return;
                }

                if (error)
                {
                    std::cerr
                        << "[http] deadline error: "
                        << error.message()
                        << std::endl;

                    return;
                }

                self->timed_out_.store(true);

                std::cerr
                    << "[http] request timeout"
                    << std::endl;

                boost::beast::error_code socketError;

                /*
                 * 先取消异步操作，再关闭 socket。
                 */
                self->socket_.cancel(socketError);

                socketError.clear();

                self->socket_.shutdown(
                    tcp::socket::shutdown_both,
                    socketError
                );

                socketError.clear();

                self->socket_.close(socketError);
            }
            catch (const std::exception& exception)
            {
                std::cerr
                    << "[http] deadline exception: "
                    << exception.what()
                    << std::endl;
            }
            catch (...)
            {
                std::cerr
                    << "[http] unknown deadline exception"
                    << std::endl;
            }
        }
    );
}

void HttpConnection::WriteResponse(
    bool responseStarted)
{
    /*
     * 普通 HTTP 错误响应，例如 404、405，
     * 会在这里设置 response_started_。
     *
     * SendJsonResponse 已经提前设置过，
     * 因此会传入 responseStarted = true。
     */
    if (!responseStarted)
    {
        bool expected = false;

        if (!response_started_
            .compare_exchange_strong(
                expected,
                true))
        {
            std::cerr
                << "[response] duplicate response ignored"
                << std::endl;

            return;
        }
    }

    /*
     * 自动设置 Content-Length 等字段。
     */
    response_.prepare_payload();

    auto self = shared_from_this();

    http::async_write(
        socket_,
        response_,
        [self](
            boost::beast::error_code error,
            std::size_t bytesTransferred)
        {
            try
            {
                if (error)
                {
                    std::cerr
                        << "[http] write error: "
                        << error.message()
                        << std::endl;

                    self->deadline_.cancel();
                    return;
                }

                boost::ignore_unused(
                    bytesTransferred
                );

                boost::beast::error_code shutdownError;

                /*
                 * 当前使用 HTTP 短连接，
                 * 响应发送完成后关闭发送端。
                 */
                self->socket_.shutdown(
                    tcp::socket::shutdown_send,
                    shutdownError
                );

                self->deadline_.cancel();
            }
            catch (const std::exception& exception)
            {
                std::cerr
                    << "[http] write exception: "
                    << exception.what()
                    << std::endl;

                self->deadline_.cancel();
            }
            catch (...)
            {
                std::cerr
                    << "[http] unknown write exception"
                    << std::endl;

                self->deadline_.cancel();
            }
        }
    );
}

void HttpConnection::HandleRequest()
{
    auto self = shared_from_this();

    response_.version(
        request_.version()
    );

    /*
     * 当前项目使用 HTTP 短连接。
     */
    response_.keep_alive(false);

    const std::string rawTarget =
        BeastViewToString(
            request_.target()
        );

    const std::string fullUrl =
        GetFullUrl();


    /*
    *
    *    printRequest(request_.method_string(),rawTarget,fullUrl);
    *
    */

    if (request_.method() == http::verb::post)
    {
        /*
         * 路由匹配时只使用路径，
         * 不包含查询参数。
         */
        const std::string postUrl =
            ExtractRoutePath(
                rawTarget
            );
        /*
        *
        * printDispatch(postUrl,fullUrl);
        *
        */

        /*
         * LogicSystem 只负责寻找并调用 Handler。
         *
         * Handler 负责：
         *
         * 1. 解析请求；
         * 2. 执行业务；
         * 3. 调用 SendJsonResponse 返回最终结果。
         */
        const bool handled =
            LogicSystem::GetInstance()
            ->HandlePost(
                postUrl,
                self
            );

        if (!handled)
        {
            /*
             * 没有找到路由属于 HTTP 分发错误，
             * 因此由 HttpConnection 返回 404。
             */
            response_.result(
                http::status::not_found
            );

            response_.set(
                http::field::server,
                "GateServer"
            );

            response_.set(
                http::field::content_type,
                "text/plain; charset=utf-8"
            );

            beast::ostream(
                response_.body()
            )
                << "The resource '"
                << postUrl
                << "' was not found.";

            std::cout
                << "[404] POST url: "
                << postUrl
                << ", full url: "
                << fullUrl
                << std::endl;

            WriteResponse();
            return;
        }

        /*
         * 路由已经成功分发。
         *
         * HttpConnection 不再自动返回 200。
         * 最终响应完全由对应 Handler 发送。
         */
        return;
    }

    /*
     * 当前只支持 POST 请求。
     */
    response_.result(
        http::status::method_not_allowed
    );

    response_.set(
        http::field::server,
        "GateServer"
    );

    response_.set(
        http::field::content_type,
        "text/plain; charset=utf-8"
    );

    response_.set(
        http::field::allow,
        "POST"
    );

    beast::ostream(
        response_.body()
    )
        << "Only POST method is supported";

    WriteResponse();
}

void HttpConnection::SendJsonResponse(
    http::status status,
    std::string jsonBody)
{
    /*
     * 确保同一个 HTTP 请求只发送一次响应。
     *
     * 这个原子判断可能在线程池工作线程中执行。
     */
    bool expected = false;

    if (!response_started_
        .compare_exchange_strong(
            expected,
            true))
    {
        std::cerr
            << "[response] duplicate response ignored"
            << std::endl;

        return;
    }

    auto self = shared_from_this();

    /*
     * Handler 可能在线程池线程中调用此函数。
     *
     * response_ 和 socket_ 的实际操作必须投递回
     * socket 对应的 Asio executor。
     */
    boost::asio::post(
        socket_.get_executor(),
        [
            self,
            status,
            jsonBody = std::move(jsonBody)
        ]() mutable
        {
            try
            {
                /*
                 * 清除可能存在的旧响应状态。
                 */
                self->response_ = {};

                self->response_.version(
                    self->request_.version()
                );

                self->response_.keep_alive(
                    false
                );

                self->response_.result(
                    status
                );

                self->response_.set(
                    http::field::server,
                    "GateServer"
                );

                self->response_.set(
                    http::field::content_type,
                    "application/json; charset=utf-8"
                );

                beast::ostream(
                    self->response_.body()
                ) << jsonBody;

                /*
                 * response_started_ 已经在本函数开头设置，
                 * 因此传入 true，避免重复原子检查。
                 */
                self->WriteResponse(true);
            }
            catch (const std::exception& exception)
            {
                std::cerr
                    << "[response] build response exception: "
                    << exception.what()
                    << std::endl;

                self->deadline_.cancel();
            }
            catch (...)
            {
                std::cerr
                    << "[response] unknown build response exception"
                    << std::endl;

                self->deadline_.cancel();
            }
        }
    );
}