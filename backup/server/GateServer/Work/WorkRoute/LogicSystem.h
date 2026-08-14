#pragma once

#include "Singleton.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

class HttpConnection;
class RequestHandler;

using ConnectionCallback =
std::function<void(std::shared_ptr<HttpConnection>)>;

/**
 * 路由分发类。
 *
 * 旧接口暂时继续支持 ConnectionCallback，方便保留现有的
 * /get_test 和 /get_verify；新的业务接口使用 RequestHandler。
 */
class LogicSystem : public Singleton<LogicSystem>
{
    friend class Singleton<LogicSystem>;

public:
    ~LogicSystem();

    LogicSystem(const LogicSystem&) = delete;
    LogicSystem& operator=(const LogicSystem&) = delete;

    // 注册面向对象的 POST Handler。
    void RegisterPostHandler(
        const std::string& url,
        std::unique_ptr<RequestHandler> handler);

    // 分发 POST 请求。
    bool HandlePost(
        const std::string& url,
        std::shared_ptr<HttpConnection> connection);

private:
    LogicSystem();

private:

    std::map<std::string, std::unique_ptr<RequestHandler>>
        post_request_handlers_;
};
