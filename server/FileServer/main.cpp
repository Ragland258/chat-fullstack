#include "ConfigMgr.h"
#include "RpcServre.h"

#include <grpcpp/grpcpp.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

int main()
{
	try
	{
		// FileServer 的监听地址由 FileServer.ini 统一管理。
		auto config = ConfigMgr::GetInstance();
		const std::string host =
			(*config)["FileServer"]["Host"];
		const std::string port =
			(*config)["FileServer"]["Port"];

		if (host.empty() || port.empty())
		{
			throw std::runtime_error(
				"FileServer Host or Port is empty");
		}

		const std::string server_address =
			host + ":" + port;

		// service 必须一直存活到 server->Wait() 返回。
		RpcServreImpl service;
		grpc::ServerBuilder builder;

		// 当前为本地开发环境，先使用不带 TLS 的 gRPC 连接。
		builder.AddListeningPort(
			server_address,
			grpc::InsecureServerCredentials());
		builder.RegisterService(&service);

		std::unique_ptr<grpc::Server> server =
			builder.BuildAndStart();

		if (!server)
		{
			throw std::runtime_error(
				"failed to start FileServer at " +
				server_address);
		}

		std::cout
			<< "FileServer listening on "
			<< server_address
			<< std::endl;

		// 阻塞主线程，由 gRPC 持续接收上传相关请求。
		server->Wait();
	}
	catch (const std::exception& exception)
	{
		std::cerr
			<< "FileServer fatal error: "
			<< exception.what()
			<< std::endl;

		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
