#ifndef SINGLETON_H
#define SINGLETON_H

#include <memory>
#include <mutex>
#include <iostream>

template <typename T>
class Singleton
{
protected:
    Singleton() = default;
    Singleton(const Singleton<T> & ) = delete;
    Singleton<T>& operator = (const Singleton<T>& ) = delete;

public:
    // 获取单例对象；首次调用时使用传入参数构造 T。
    template <typename ...Args>
    static std::shared_ptr<T> Getinstance(Args&&... args)
    {
        static std::shared_ptr<T> _instance(new T(std::forward<Args>(args)...));
        return _instance;
    }

    virtual ~Singleton(){}
};


#endif // SINGLETON_H
