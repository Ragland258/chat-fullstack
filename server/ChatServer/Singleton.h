#pragma once

#include "const.h"

template <typename T>
class Singleton
{
public:

	template <typename... Args>
	static std::shared_ptr<T> GetInstance(Args&& ...args)
	{
		static std::shared_ptr<T> instance(new T(std::forward<Args>(args)...));
		return instance;
	}

	virtual ~Singleton() = 0;

protected:
	Singleton() = default;
	Singleton(const Singleton&) = delete;
	Singleton& operator=(const Singleton&) = delete;

};

template <typename T>
Singleton<T>::~Singleton() {}
