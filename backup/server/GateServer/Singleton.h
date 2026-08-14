#pragma once
#include <memory>
#include <utility>


template<typename T>
class Singleton
{
protected:
	Singleton() = default;
	Singleton(const Singleton&) = delete;
	Singleton& operator=(const Singleton&) = delete;

public:
	template <typename... Args>
	static std::shared_ptr<T> GetInstance(Args&& ...args)
	{
		static std::shared_ptr<T> instance(new T(std::forward<Args>(args)...));
		return instance;
	}

	virtual ~Singleton() = default;
};

