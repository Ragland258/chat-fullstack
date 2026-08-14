#include "StatusServiceImpl.h"

#include "ConfigMgr.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <grpcpp/grpcpp.h>

#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

/*
 * 启动 Status gRPC Server。
 *
 * 执行流程：
 *
 * 1. 从配置文件读取监听地址；
 * 2. 创建 StatusServiceImpl；
 * 3. 创建 grpc::ServerBuilder；
 * 4. 注册服务；
 * 5. 启动 gRPC Server；
 * 6. 监听 Ctrl+C 和终止信号；
 * 7. 收到信号后调用 server->Shutdown()；
 * 8. 等待 gRPC Server 完全退出。
 */
void runServer()
{
    /*
     * 获取配置管理器单例。
     */
    auto config =
        ConfigMgr::GetInstance();

    /*
     * 读取 Status Server 配置。
     *
     * config.ini 示例：
     *
     * [StatusServer]
     * Host=127.0.0.1
     * Port=50052
     */
    const std::string host =
        (*config)["StatusServer"]["Host"];

    const std::string port =
        (*config)["StatusServer"]["Port"];

    /*
     * 检查配置是否存在。
     */
    if (host.empty() || port.empty())
    {
        throw std::runtime_error(
            "StatusServer Host or Port is empty"
        );
    }

    /*
     * gRPC 监听地址格式：
     *
     * 127.0.0.1:50052
     */
    const std::string serverAddress =
        host + ":" + port;

    /*
     * 创建 StatusService 的具体实现对象。
     *
     * service 的生命周期必须覆盖整个 gRPC Server
     * 运行期间，因此它定义在 runServer() 的局部作用域中，
     * 且 runServer() 在服务器关闭前不会返回。
     */
    StatusServiceImpl service;

    /*
     * 创建 gRPC Server 构建器。
     */
    grpc::ServerBuilder builder;

    /*
     * 添加监听端口。
     *
     * InsecureServerCredentials 表示不使用 TLS。
     * 开发环境可以使用，生产环境建议配置 TLS。
     */
    builder.AddListeningPort(
        serverAddress,
        grpc::InsecureServerCredentials()
    );

    /*
     * 注册 StatusServiceImpl。
     *
     * 客户端调用 GetChatServer 时，
     * gRPC 会进入 StatusServiceImpl::GetChatServer。
     */
    builder.RegisterService(
        &service
    );

    /*
     * 构建并启动 gRPC Server。
     */
    std::unique_ptr<grpc::Server> server =
        builder.BuildAndStart();

    /*
     * BuildAndStart 失败时会返回空指针。
     *
     * 常见原因：
     *
     * 1. 端口已经被其他程序占用；
     * 2. 地址配置错误；
     * 3. gRPC 初始化失败。
     */
    if (!server)
    {
        throw std::runtime_error(
            "Failed to start StatusServer at " +
            serverAddress
        );
    }

    std::cout
        << "StatusServer listening on "
        << serverAddress
        << std::endl;

    /*
     * 创建 Boost.Asio io_context。
     *
     * 这里只用它监听操作系统终止信号。
     */
    boost::asio::io_context ioc;

    /*
     * 注册需要监听的信号：
     *
     * SIGINT：
     *     通常由 Ctrl+C 触发。
     *
     * SIGTERM：
     *     通常由操作系统或服务管理程序触发。
     */
    boost::asio::signal_set signals(
        ioc,
        SIGINT,
        SIGTERM
    );

    /*
     * 异步等待终止信号。
     */
    signals.async_wait(
        [&server, &ioc](
            const boost::system::error_code& error,
            int signalNumber)
        {
            /*
             * 如果异步等待本身发生错误，
             * 不执行关闭操作。
             */
            if (error)
            {
                std::cerr
                    << "Signal wait error: "
                    << error.message()
                    << std::endl;

                return;
            }

            std::cout
                << "Signal "
                << signalNumber
                << " received. Shutting down StatusServer..."
                << std::endl;

            /*
             * Shutdown 会通知 gRPC Server 停止接收新请求，
             * 并让 server->Wait() 返回。
             *
             * Shutdown 是线程安全的，
             * 可以从信号监听线程调用。
             */
            server->Shutdown();

            /*
             * 停止 Asio 事件循环。
             */
            ioc.stop();
        }
    );

    /*
     * 创建单独线程运行信号监听。
     *
     * 不能使用 detach()。
     *
     * 因为线程中的 lambda 引用了局部变量 ioc，
     * 如果线程被 detach，而 runServer 返回，
     * ioc 会被销毁，线程可能访问失效对象。
     */
    std::thread signalThread(
        [&ioc]()
        {
            try
            {
                ioc.run();
            }
            catch (const std::exception& exception)
            {
                std::cerr
                    << "Signal thread exception: "
                    << exception.what()
                    << std::endl;
            }
        }
    );

    /*
     * 阻塞当前线程，持续等待 gRPC 请求。
     *
     * 当信号处理函数调用 server->Shutdown() 后，
     * Wait() 才会返回。
     */
    server->Wait();

    /*
     * 确保 io_context 停止。
     *
     * 正常情况下信号处理函数已经调用过 ioc.stop()；
     * 这里再次调用属于防御性处理。
     */
    ioc.stop();

    /*
     * 等待信号线程结束。
     *
     * 不要 detach。
     */
    if (signalThread.joinable())
    {
        signalThread.join();
    }

    std::cout
        << "StatusServer stopped."
        << std::endl;
}

/*
 * 程序入口。
 */
int main()
{
    try
    {
        runServer();
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "StatusServer fatal error: "
            << exception.what()
            << std::endl;

        return EXIT_FAILURE;
    }
    catch (...)
    {
        std::cerr
            << "StatusServer unknown fatal error."
            << std::endl;

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
