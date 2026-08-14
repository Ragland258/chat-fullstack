#include "IOSPool.h"
#include "const.h"

boost::asio::io_context& IOSPool::GetIOService()
{
	auto& service = ioServices_[nextIOS_++];
	if (nextIOS_ == ioServices_.size())
		nextIOS_ = 0;
	return *service;

}

void IOSPool::Stop()
{
	std::scoped_lock lock(stop_mutex_);
	if (stopped_)
	{
		return;
	}
	stopped_ = true;

	for (auto& work : works_)
		work.reset();
	for (auto& service : ioServices_)
		service->stop();
	for (auto& t : threads_)
	{
		if (t.joinable())
			t.join();
	}
}

IOSPool::IOSPool(std::size_t size) :nextIOS_(0)
{
	if (size == 0)
		size = 1;

	ioServices_.reserve(size);
	works_.reserve(size);
	threads_.reserve(size);

	for (std::size_t i = 0; i < size; i++)
	{
		auto service = std::make_unique<IOService>();
		works_.push_back(
			std::make_unique<WorkGuard>(service->get_executor())
		);
		ioServices_.push_back(std::move(service));
	}

	for (std::size_t i = 0; i < size; i++)
	{
		threads_.emplace_back([this, i]()
			{
				ioServices_[i]->run();
			});
	}
}

