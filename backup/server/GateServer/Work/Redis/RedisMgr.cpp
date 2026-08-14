#include "RedisMgr.h"
#include "ConfigMgr.h"
#include <openssl/rand.h>  // OpenSSL 的安全随机数生成函数 RAND_bytes
RedisMgr::RedisMgr()
{
}

RedisMgr::~RedisMgr()
{
    Close();
}

void RedisMgr::Close()
{
    RedisPool::GetInstance()->Close();
}


/*
 * 匿名命名空间。
 *
 * 这里定义的变量和函数只能在当前 .cpp 文件中使用，
 * 其他 .cpp 文件无法直接访问，避免名字冲突。
 */
namespace
{
    /*
     * token 使用 32 字节随机数据。
     *
     * 1 字节 = 8 bit
     * 32 字节 = 256 bit
     *
     * 后面转换成十六进制字符串后：
     * 每个字节转换成两个字符，
     * 所以最终 token 长度为 64 个字符。
     */
    constexpr std::size_t LOGIN_TOKEN_BYTES = 32;

    /*
     * Redis key 的固定前缀。
     *
     * 假设 email 是：
     *     user@example.com
     *
     * 最终 Redis key 是：
     *     login:token:user@example.com
     */
    constexpr const char* LOGIN_TOKEN_KEY_PREFIX =
        "login:token:";

    /*
     * 根据 email 生成 Redis key。
     *
     * 参数：
     *     email：用户邮箱
     *
     * 返回：
     *     login:token: + email
     *
     * 示例：
     *     email = "user@example.com"
     *
     * 返回：
     *     "login:token:user@example.com"
     */
    std::string MakeLoginTokenKey(
        const std::string& email
    )
    {
        /*
         * 目前没有对 email 做哈希处理，
         * 直接把 email 拼接到 Redis key 后面。
         */
        return std::string(LOGIN_TOKEN_KEY_PREFIX) + email;
    }

    /*
     * 把二进制数据转换成十六进制字符串。
     *
     * RAND_bytes 生成的是二进制数据，
     * 二进制数据可能包含 '\0' 等不可打印字符，
     * 所以不能直接当作普通字符串返回给客户端。
     *
     * 这里将每个字节转换成两个十六进制字符。
     *
     * 示例：
     *     一个字节 0xAF
     *
     * 转换后：
     *     "af"
     *
     * 参数：
     *     data  ：二进制数据的首地址
     *     length：二进制数据的字节数
     *
     * 返回：
     *     十六进制字符串
     */
    std::string HexEncode(
        const unsigned char* data,
        std::size_t length
    )
    {
        /*
         * 十六进制字符表。
         *
         * 数字 0~15 分别对应：
         * 0 1 2 3 4 5 6 7 8 9 a b c d e f
         */
        static constexpr char HEX_TABLE[] =
            "0123456789abcdef";

        /*
         * 一个字节需要两个十六进制字符表示。
         *
         * 例如：
         *     32 字节二进制数据
         *
         * 转换后：
         *     64 个字符
         *
         * '\0' 只是先用来初始化字符串内容，
         * 后面的循环会把这些位置全部覆盖掉。
         */
        std::string result(length * 2, '\0');

        /*
         * 逐个处理原始数据中的每一个字节。
         */
        for (std::size_t i = 0; i < length; ++i)
        {
            /*
             * 一个字节有 8 bit，例如：
             *
             *     data[i] = 1010 1111
             *
             * 一个十六进制字符只能表示 4 bit，
             * 所以需要把这个字节分成：
             *
             *     高 4 位：1010
             *     低 4 位：1111
             */

             /*
              * 获取高 4 位。
              *
              * data[i] >> 4：
              *     将数据右移 4 位。
              *
              * 例如：
              *     1010 1111
              * 变成：
              *     0000 1010
              *
              * & 0x0F：
              *     只保留最低 4 位。
              *
              * 结果是十进制 10，
              * HEX_TABLE[10] 就是字符 'a'。
              */
            result[i * 2] =
                HEX_TABLE[(data[i] >> 4) & 0x0F];

            /*
             * 获取低 4 位。
             *
             * data[i] & 0x0F：
             *     只保留最低 4 位。
             *
             * 例如：
             *     1010 1111
             * 变成：
             *     0000 1111
             *
             * 结果是十进制 15，
             * HEX_TABLE[15] 就是字符 'f'。
             */
            result[i * 2 + 1] =
                HEX_TABLE[data[i] & 0x0F];
        }

        /*
         * 返回转换后的十六进制字符串。
         */
        return result;
    }

    /*
     * 生成安全的随机登录 token。
     *
     * 参数：
     *     outputToken：用来接收生成后的 token
     *
     * 返回：
     *     true ：生成成功
     *     false ：生成失败
     */
    bool GenerateSecureLoginToken(
        std::string& outputToken
    )
    {
        /*
         * 先清空调用方传入的字符串。
         *
         * 这样即使后面生成失败，
         * outputToken 也不会保留之前的旧值。
         */
        outputToken.clear();

        /*
         * 创建一个长度为 32 字节的固定数组。
         *
         * unsigned char 通常正好占一个字节，
         * 很适合保存原始二进制随机数据。
         *
         * bytes{} 表示将数组初始化为全 0。
         */
        std::array<
            unsigned char,
            LOGIN_TOKEN_BYTES
        > bytes{};

        /*
         * 使用 OpenSSL 生成安全随机数据。
         *
         * bytes.data()：
         *     返回数组首地址，RAND_bytes 会把随机数据写入这里。
         *
         * bytes.size()：
         *     返回数组长度，也就是 32。
         *
         * RAND_bytes 成功时返回 1，
         * 失败时返回其他值。
         */
        if (RAND_bytes(
            bytes.data(),
            static_cast<int>(bytes.size())
        ) != 1)
        {
            /*
             * 安全随机数生成失败。
             */
            return false;
        }

        /*
         * 现在 bytes 中是 32 字节二进制随机数据。
         *
         * 例如其中可能包含：
         *     0xA2、0x00、0xFF 等数据。
         *
         * 将它转换成可打印的十六进制字符串。
         *
         * 32 字节最终会得到 64 字符 token。
         */
        outputToken = HexEncode(
            bytes.data(),
            bytes.size()
        );

        /*
         * token 生成成功。
         */
        return true;
    }
}


VerifyCodeResult RedisMgr::ConsumeVerifyCode(
    const std::string& key,
    const std::string& inputCode
)
{
    static const std::string luaScript = R"lua(
local storedCode = redis.call("GET", KEYS[1])

if not storedCode then
    return 0
end

if storedCode ~= ARGV[1] then
    return -1
end

redis.call("DEL", KEYS[1])

return 1
)lua";

    RedisConGuard guard(
        RedisPool::GetInstance()->BorrowConnect()
    );

    auto* connection = guard.get();

    if (!connection)
    {
        std::cerr
            << "[ConsumeVerifyCode] no available Redis connection"
            << std::endl;

        return VerifyCodeResult::RedisError;
    }

    RedisReplyMgr reply;

    /*  EVAL
    *   Lua脚本
    *   1个Redis key
    *   key
    *    用户输入的验证码
    */
    reply = reinterpret_cast<redisReply*>(
        redisCommand(
            connection,
            "EVAL %b 1 %b %b",

            luaScript.data(),
            static_cast<size_t>(luaScript.size()),

            key.data(),
            static_cast<size_t>(key.size()),

            inputCode.data(),
            static_cast<size_t>(inputCode.size())
        )
        );

    if (!reply)
    {
        std::cerr
            << "[ConsumeVerifyCode] Redis returned no reply"
            << std::endl;

        return VerifyCodeResult::RedisError;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr
            << "[ConsumeVerifyCode] Lua error: "
            << (reply->str ? reply->str : "unknown error")
            << std::endl;

        return VerifyCodeResult::RedisError;
    }

    if (reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr
            << "[ConsumeVerifyCode] unexpected reply type: "
            << reply->type
            << std::endl;

        return VerifyCodeResult::RedisError;
    }

    switch (reply->integer)
    {
    case 1:
        return VerifyCodeResult::Success;

    case 0:
        return VerifyCodeResult::CodeMissing;

    case -1:
        return VerifyCodeResult::CodeMismatch;

    default:
        return VerifyCodeResult::RedisError;
    }
}

std::string RedisMgr::CreateLoginToken(const std::string& email, int expireSeconds)
{

    if (email.empty() || expireSeconds <= 0)
    {
        std::cerr
            << "[CreateLoginToken] invalid arguments"
            << std::endl;

        return "";
    }

    std::string generatedToken;

    if (!GenerateSecureLoginToken(generatedToken))
    {
        std::cerr
            << "[CreateLoginToken] token generation failed"
            << std::endl;

        return "";
    }

    const std::string redisKey =
        MakeLoginTokenKey(email);

    /*
     * KEYS[1]：login:token:<email>
     * ARGV[1]：新 token
     * ARGV[2]：有效期，单位秒
     *
     * 如果 key 已经存在，直接覆盖旧 token。
     */
    static const std::string luaScript = R"lua(
local token = ARGV[1]
local ttl = tonumber(ARGV[2])

if not token or token == "" then
    return -1
end

if not ttl or ttl <= 0 then
    return -2
end

redis.call(
    "SET",
    KEYS[1],
    token,
    "EX",
    ttl
)

return 1
)lua";

    RedisConGuard guard(
        RedisPool::GetInstance()->BorrowConnect()
    );

    auto* connection = guard.get();

    if (!connection)
    {
        std::cerr
            << "[CreateLoginToken] no available Redis connection"
            << std::endl;

        return "";
    }

    RedisReplyMgr reply;

    reply = reinterpret_cast<redisReply*>(
        redisCommand(
            connection,
            "EVAL %b 1 %b %b %d",

            luaScript.data(),
            static_cast<std::size_t>(luaScript.size()),

            redisKey.data(),
            static_cast<std::size_t>(redisKey.size()),

            generatedToken.data(),
            static_cast<std::size_t>(generatedToken.size()),

            expireSeconds
        )
        );

    if (!reply)
    {
        std::cerr
            << "[CreateLoginToken] Redis returned no reply"
            << std::endl;

        return "";
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr
            << "[CreateLoginToken] Lua error: "
            << (reply->str ? reply->str : "unknown error")
            << std::endl;

        return "";
    }

    if (reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr
            << "[CreateLoginToken] unexpected reply type: "
            << reply->type
            << std::endl;

        return "";
    }

    if (reply->integer != 1)
    {
        std::cerr
            << "[CreateLoginToken] Lua returned: "
            << reply->integer
            << std::endl;

        return "";
    }

    // Redis 写入成功后，才向调用方返回 token。
    return std::move(generatedToken);

}

bool RedisMgr::VerifyLoginToken(const std::string& email, const std::string& token)
{
    std::string storedToken;
    if(!Get(email, storedToken))
    {
        std::cerr
            << "[VerifyLoginToken] failed to get token from Redis"
            << std::endl;
        return false;
	}
	return storedToken == token;
}

bool RedisMgr::Connect(const std::string& host, int port)
{
    auto* context = RedisPool::GetInstance()->BorrowConnect();
    if (!context)
    {
        std::cout << "RedisPool is empty, connect failed" << std::endl;
        return false;
    }
    RedisPool::GetInstance()->ReturnConnect(context);
    return true;
}

bool RedisMgr::Get(const std::string& key, std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[Get " << key << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "GET %s", key.c_str());
    if (!reply)
    {
        std::cout << "[Get " << key << "] failed" << std::endl;
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        std::cout << "[Get " << key << "] failed, type is not string" << std::endl;
        return false;
    }
    value = reply->str;
    return true;
}

bool RedisMgr::Set(const std::string& key, const std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[Set " << key << " " << value << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "SET %s %s", key.c_str(), value.c_str());
    if (!reply)
    {
        std::cout << "Execut command [ SET " << key << " " << value << " ] failure " << std::endl;
        return false;
    }
    if (!(reply->type == REDIS_REPLY_STATUS &&
          (std::strcmp(reply->str, "OK") == 0 || std::strcmp(reply->str, "ok") == 0)))
    {
        std::cout << "Execut command [ SET " << key << " " << value << " ] failure " << std::endl;
        return false;
    }
    std::cout << "Execut command [ SET " << key << "  " << value << " ] success ! " << std::endl;
    return true;
}

bool RedisMgr::Auth(const std::string& password)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "Auth failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "AUTH %s", password.c_str());
    if (!reply)
    {
        std::cout << "Auth failed" << std::endl;
        return false;
    }
    if (reply->type != REDIS_REPLY_STATUS || std::strcmp(reply->str, "OK") != 0)
    {
        std::cout << "Auth failed: " << (reply->str ? reply->str : "") << std::endl;
        return false;
    }
    std::cout << "Auth success" << std::endl;
    return true;
}

bool RedisMgr::LPush(const std::string& key, const std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[LPush " << key << " " << value << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "LPUSH %s %s", key.c_str(), value.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ LPUSH " << key << " " << value << " ] failure " << std::endl;
        return false;
    }
    std::cout << "Execut command [ LPUSH " << key << " " << value << " ] success ! " << std::endl;
    return true;
}

bool RedisMgr::LPop(const std::string& key, std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[LPop " << key << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "LPOP %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_STRING)
    {
        std::cout << "Execut command [ LPOP " << key << " ] failure " << std::endl;
        return false;
    }
    value = reply->str;
    return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[RPush " << key << " " << value << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "RPUSH %s %s", key.c_str(), value.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ RPUSH " << key << " " << value << " ] failure " << std::endl;
        return false;
    }
    std::cout << "Execut command [ RPUSH " << key << " " << value << " ] success ! " << std::endl;
    return true;
}

bool RedisMgr::RPop(const std::string& key, std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[RPop " << key << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "RPOP %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_STRING)
    {
        std::cout << "Execut command [ RPOP " << key << " ] failure " << std::endl;
        return false;
    }
    value = reply->str;
    return true;
}

bool RedisMgr::HSet(const std::string& key, const std::string& hkey, const std::string& value)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[HSet " << key << " " << hkey << " " << value << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "HSET %s %s %s",
                                      key.c_str(), hkey.c_str(), value.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ HSET " << key << " " << hkey << " " << value << " ] failure " << std::endl;
        return false;
    }
    std::cout << "Execut command [ HSET " << key << " " << hkey << " " << value << " ] success ! " << std::endl;
    return true;
}

bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[HSet binary] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "HSET %s %s %b",
                                      key, hkey, hvalue, hvaluelen);
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ HSET binary ] failure " << std::endl;
        return false;
    }
    std::cout << "Execut command [ HSET binary ] success ! " << std::endl;
    return true;
}

std::string RedisMgr::HGet(const std::string& key, const std::string& hkey)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[HGet " << key << " " << hkey << "] failed, no available connection" << std::endl;
        return "";
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "HGET %s %s", key.c_str(), hkey.c_str());
    if (!reply || reply->type != REDIS_REPLY_STRING)
    {
        std::cout << "Execut command [ HGET " << key << " " << hkey << " ] failure " << std::endl;
        return "";
    }
    return reply->str;
}

bool RedisMgr::Del(const std::string& key)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[Del " << key << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "DEL %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ DEL " << key << " ] failure " << std::endl;
        return false;
    }
    std::cout << "Execut command [ DEL " << key << " ] success ! " << std::endl;
    return true;
}

bool RedisMgr::ExistsKey(const std::string& key)
{
    RedisConGuard guard(RedisPool::GetInstance()->BorrowConnect());
    auto* connect_ = guard.get();
    if (!connect_)
    {
        std::cout << "[ExistsKey " << key << "] failed, no available connection" << std::endl;
        return false;
    }

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(connect_, "EXISTS %s", key.c_str());
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        std::cout << "Execut command [ EXISTS " << key << " ] failure " << std::endl;
        return false;
    }
    return reply->integer != 0;
}

RedisPool::~RedisPool()
{
    Close();
    std::lock_guard<std::mutex> lock(mutex_);
    while (!connections_.empty())
    {
        redisFree(connections_.front());
        connections_.pop();
    }
}

redisContext* RedisPool::BorrowConnect()
{
    while (true)
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

        auto* context = connections_.front();
        connections_.pop();
        //std::cout << "[RedisPool] borrow, remaining: " << connections_.size() << std::endl;
        lk.unlock();

        if (CheckConnection(context))
            return context;

        //std::cout << "[RedisPool] borrowed connection is not alive, reconnect" << std::endl;
        redisFree(context);
        auto* new_context = CreateConnection();
        if (new_context)
            return new_context;

        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_one();
    }
}

void RedisPool::ReturnConnect(redisContext* context)
{
    if (!context)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (b_stop_)
    {
        redisFree(context);
        return;
    }

    if (!CheckConnection(context))
    {
        std::cout << "[RedisPool] return bad connection, reconnect" << std::endl;
        redisFree(context);
        auto* new_context = CreateConnection();
        if (new_context)
            connections_.push(new_context);
        cv_.notify_one();
        return;
    }

    connections_.push(context);
    //std::cout << "[RedisPool] return, remaining: " << connections_.size() << std::endl;
    cv_.notify_one();
}

void RedisPool::Close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    b_stop_ = true;
    std::cout << "[RedisPool] closing, destroy " << connections_.size() << " connections" << std::endl;
    while (!connections_.empty())
    {
        redisFree(connections_.front());
        connections_.pop();
    }
    cv_.notify_all();
}

RedisPool::RedisPool()
    : b_stop_(false), pool_size_(0)
{
    auto config = ConfigMgr::GetInstance();

    host_ = (*config)["RedisServer"]["Host"];
    port_ = (*config)["RedisServer"]["Port"];
    pool_size_ = atoi((*config)["RedisServer"]["size"].c_str());
    password_ = (*config)["RedisServer"]["password"];
    std::cout << "[RedisPool] config: host=" << host_ << ", port=" << port_
              << ", pwd_len=" << password_.length() << std::endl;

    for (int i = 0; i < pool_size_; ++i)
    {
        auto* context = CreateConnection();
        if (context)
        {
            connections_.push(context);
            //std::cout << "[RedisPool] connection " << connections_.size() << "/" << pool_size_ << " created" << std::endl;
        }
    }
    std::cout << "[RedisPool] init " << connections_.size() << "/" << pool_size_ << " connections" << std::endl;
}

redisContext* RedisPool::CreateConnection()
{
    auto* context = redisConnect(host_.c_str(), atoi(port_.c_str()));
    if (context == nullptr || context->err != 0)
    {
        std::cout << "[RedisPool] connect failed: "
                  << (context ? context->errstr : "null context") << std::endl;
        if (context)
            redisFree(context);
        return nullptr;
    }

    if (!password_.empty())
    {
        RedisReplyMgr reply;
        reply = (redisReply*)redisCommand(context, "AUTH %s", password_.c_str());
        if (!reply || reply->type == REDIS_REPLY_ERROR)
        {
            std::cout << "[RedisPool] auth failed: "
                      << (reply ? reply->str : "no reply") << std::endl;
            redisFree(context);
            return nullptr;
        }
    }

    return context;
}

bool RedisPool::CheckConnection(redisContext* context)
{
    if (!context || context->err != 0)
        return false;

    RedisReplyMgr reply;
    reply = (redisReply*)redisCommand(context, "PING");
    if (!reply)
        return false;

    return reply->type == REDIS_REPLY_STATUS &&
           reply->str &&
           std::strcmp(reply->str, "PONG") == 0;
}
