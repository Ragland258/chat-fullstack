#pragma once

#include "const.h"
#include "Singleton.h"
#include <cstdint>
#include <optional>
#include <hiredis/hiredis.h>

enum class VerifyCodeResult : int
{
    RedisError = -2,
    CodeMismatch = -1,
    CodeMissing = 0,
    Success = 1
};

struct LoginSessionInfo
{
    std::uint64_t uid{ 0 };
    std::uint64_t device_id{ 0 };
    std::string email;
    std::string server_id;
};

struct RedisReplyMgr
{
    RedisReplyMgr()
        : reply_(nullptr)
    {
    }

    explicit RedisReplyMgr(redisReply* r)
        : reply_(r)
    {
    }

    ~RedisReplyMgr()
    {
        if (reply_)
            freeReplyObject(reply_);
    }

    operator redisReply*() const
    {
        return reply_;
    }

    redisReply* operator->() const
    {
        return reply_;
    }

    redisReply& operator*() const
    {
        assert(reply_ != nullptr);
        return *reply_;
    }

    explicit operator bool() const noexcept
    {
        return reply_ != nullptr;
    }

    RedisReplyMgr(const RedisReplyMgr&) = delete;
    RedisReplyMgr& operator=(const RedisReplyMgr&) = delete;

    RedisReplyMgr(RedisReplyMgr&& other) noexcept
        : reply_(other.reply_)
    {
        other.reply_ = nullptr;
    }

    RedisReplyMgr& operator=(RedisReplyMgr&& other) noexcept
    {
        if (this != &other)
        {
            if (reply_)
                freeReplyObject(reply_);
            reply_ = other.reply_;
            other.reply_ = nullptr;
        }
        return *this;
    }

    RedisReplyMgr& operator=(redisReply* other)
    {
        if (other == reply_)
            return *this;

        if (reply_)
            freeReplyObject(reply_);
        reply_ = other;
        return *this;
    }

private:
    redisReply* reply_;
};

class RedisPool : public Singleton<RedisPool>
{
    friend class Singleton<RedisPool>;
public:
    ~RedisPool();
    RedisPool(const RedisPool&) = delete;
    RedisPool& operator=(const RedisPool&) = delete;

    redisContext* BorrowConnect();
    void ReturnConnect(redisContext* context);
    void Close();

private:
    RedisPool();
    redisContext* CreateConnection();
    bool CheckConnection(redisContext* context);

private:
    std::atomic<bool> b_stop_;
    size_t pool_size_;
    std::string host_;
    std::string port_;
    std::string password_;
    std::queue<redisContext*> connections_;
    std::mutex mutex_;
    std::condition_variable cv_;

};

class RedisConGuard
{
public:
    explicit RedisConGuard(redisContext* context)
        : context_(context)
    {
    }

    ~RedisConGuard()
    {
        if (context_)
            RedisPool::GetInstance()->ReturnConnect(context_);
    }

    RedisConGuard(const RedisConGuard&) = delete;
    RedisConGuard& operator=(const RedisConGuard&) = delete;

    redisContext* get() const
    {
        return context_;
    }

    redisContext* operator->() const
    {
        return context_;
    }

private:
    redisContext* context_;
};

class RedisMgr : public Singleton<RedisMgr>,
    public std::enable_shared_from_this<RedisMgr>
{
    friend class Singleton<RedisMgr>;

public:

    VerifyCodeResult ConsumeVerifyCode(
        const std::string& key,
        const std::string& inputCode
    );

    // 创建一条设备级登录会话，并为整个 Redis Hash 设置 TTL。
    std::string CreateLoginSession(
        std::uint64_t uid,
        std::uint64_t deviceId,
        const std::string& email,
        const std::string& serverId,
        int expireSeconds = 30 * 60
    );

    // 校验会话安全字段，并返回 Redis 中可信的 Email。
    std::optional<LoginSessionInfo> VerifyLoginSession(
        std::uint64_t uid,
        std::uint64_t deviceId,
        const std::string& token,
        const std::string& serverId
	);


    void Close();
    ~RedisMgr();
private:

    bool Connect(const std::string& host, int port);
    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);
    bool Auth(const std::string& password);
    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);
    bool HSet(const std::string& key, const std::string& hkey, const std::string& value);
    bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
    std::string HGet(const std::string& key, const std::string& hkey);
    bool Del(const std::string& key);
    bool ExistsKey(const std::string& key);


private:
    RedisMgr();
};

