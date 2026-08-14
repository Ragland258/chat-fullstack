#pragma once
#include "const.h"
#include "Singleton.h"

class IOSPool:public Singleton<IOSPool>
{
	friend class Singleton<IOSPool>;
public:
	using IOService = boost::asio::io_context;
	using WorkGuard = boost::asio::executor_work_guard<IOService::executor_type>;
	using WorkPtr = std::unique_ptr<WorkGuard>;

	~IOSPool() { Stop(); }
	IOSPool(const IOSPool&) = delete;
	IOSPool& operator = (const IOSPool&) = delete;
	boost::asio::io_context& GetIOService();
	void Stop();

private:
	IOSPool(std::size_t size = std::thread::hardware_concurrency());

private:
	std::vector<IOService> ioServices_;
	std::vector<WorkPtr> works_;
	std::vector<std::thread> threads_;
	std::size_t nextIOS_;

};

