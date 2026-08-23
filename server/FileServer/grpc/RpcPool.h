#pragma once
#include "const.h"
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "message.pb.h"

template <typename T>
class RpcPool
{
public:

	/*
* T 是 proto 生成的 Service 类型，例如：
*
* message::VarifyService
* message::StatusService
*
* T::Stub 就是对应服务的客户端 Stub。
*/
    using Stub = typename T::Stub;

	void initPool(
		const std::string& host,
		const std::string& port,
		std::size_t poolSize);

	RpcPool():b_stop_(false) {}
	~RpcPool();

	RpcPool(const RpcPool&) = delete;
	RpcPool& operator=(const RpcPool&) = delete;

	/*
	 * @brief 停止RPC连接池
	 * @details 停止连接池,将所有连接放回连接池,等待被其他线程获取
	 */
	void Stop();

	/*
		 * @brief 获取RPC连接
		 * @details 从连接池中获取一个RPC连接,如果连接池为空,则等待连接池有连接
		 * @return std::unique_ptr<VarifyService::Stub> RPC连接
		 */
	std::unique_ptr<Stub> Borrow();

	/*
	 * @brief 回收RPC连接
	 * @details 将连接池中的连接放回连接池,等待被其他线程获取
	 * @param stub RPC连接
	 */
	void Return(std::unique_ptr<Stub>&& stub);
private:
	std::string host_;
	std::string port_;
	std::size_t poolSize_;
	std::atomic<bool> b_stop_;//是否回收
	std::shared_ptr<grpc::Channel> channel_; // 共用的rpc channel,每个stub都可以使用这个channel
	std::queue<std::unique_ptr<Stub>> connections_;//使用互斥锁的队列,性能不高,后续修改
	std::mutex mutex_;
	std::condition_variable cv_;

};

template <typename T>
class RpcPoolGuard
{
	using Stub = typename T::Stub;
	using Pool = RpcPool<T>;

public:

	RpcPoolGuard(Pool& pool)
		: pool_(pool), stub_(pool_.Borrow())
	{
	}

	~RpcPoolGuard()
	{
		if (stub_)
		{
			pool_.Return(std::move(stub_));
		}
	}

	RpcPoolGuard(const RpcPoolGuard&) = delete;
	RpcPoolGuard& operator=(const RpcPoolGuard&) = delete;

	Stub* operator->()
	{
		return stub_.get();
	}

	Stub* GetStub()
	{
		return stub_.get();
	}	

	operator bool() const
	{
		return stub_ != nullptr;
	}
private:
	Pool& pool_;
	std::unique_ptr<Stub> stub_;

};

template<typename T>
inline void RpcPool<T>::initPool(const std::string& host, const std::string& port, std::size_t poolSize)
{
	host_ = host;
	port_ = port;
	poolSize_ = poolSize;
	channel_ = grpc::CreateChannel(host + ":" + port,
		grpc::InsecureChannelCredentials());
	for(int i = 0; i < poolSize; ++i)
	{
		connections_.push((T::NewStub(channel_)));
	}
}

template<typename T>
inline RpcPool<T>::~RpcPool()
{
	Stop();
	while (!connections_.empty())
		connections_.pop();
}

template<typename T>
inline void RpcPool<T>::Stop()
{
	if (b_stop_.exchange(true))
	{
		// 已经停止过。
		return;
	}

	/*
	 * 唤醒所有正在 Borrow() 中等待的线程。
	 */
	cv_.notify_all();
}

template<typename T>
inline std::unique_ptr<typename RpcPool<T>::Stub> RpcPool<T>::Borrow()
{
	std::unique_lock<std::mutex> lk(mutex_);
	cv_.wait(lk, [this]()
		{
			if (b_stop_)
				return true;
			return !connections_.empty();
		});

	if (b_stop_)
		return nullptr;
	auto stub = std::move(connections_.front());
	connections_.pop();
	return stub;
}

template<typename T>
inline void RpcPool<T>::Return(std::unique_ptr<typename RpcPool<T>::Stub>&& stub)
{
	std::lock_guard<std::mutex> lk(mutex_);
	if (b_stop_)
		return;
	connections_.push(std::move(stub));
	cv_.notify_one();
}