#include "IOSPool.h"
#include "Work/Grpc/VerifyGrpcClient.h"


boost::asio::io_context& IOSPool::GetIOService()
{
	auto& service = ioServices_[nextIOS_++];
	if (nextIOS_ == ioServices_.size())
		nextIOS_ = 0;
	return service;

}

void IOSPool::Stop()
{

	for (auto& work : works_)
		work.reset();
	for (auto& t : threads_)
		t.join();
}

IOSPool::IOSPool(std::size_t size):ioServices_(size),works_(size),nextIOS_(0)
{
	for (std::size_t i = 0; i < size; i++)
		works_[i] = std::make_unique<WorkGuard>(ioServices_[i].get_executor());

	for (auto i = 0; i < size; i++)
	{
		threads_.emplace_back([this, i]()
			{
				ioServices_[i].run();
			});
	}
}

