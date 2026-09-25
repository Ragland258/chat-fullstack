#include "redis/RedisFileMgr.h"
#include "redis/RedisMgr.h"

// createIpload 用到的辅助函数
namespace
{
    constexpr std::string_view kUploadSessionPrefix{
        "upload:session:{"
    };

    /*
     * Redis Key 示例：
     *
     * upload:session:{a32178f2-d7e0-4af5-9d49-4eae32a84c31}
     *
     * 花括号中的 sessionId 是 Redis Cluster hash tag。
     * 将来同一个上传会话的 session、parts 和 lock Key
     * 可以稳定地落到同一个 hash slot。
     */
    std::string MakeUploadSessionKey(
        const std::string& sessionId)
    {
        return std::string{ kUploadSessionPrefix } +
            sessionId +
            "}";
    }


    std::string_view UploadModeToText(
        UploadMode mode) noexcept
    {
        switch (mode)
        {
        case UploadMode::SinglePut:
            return "single_put";

        case UploadMode::Multipart:
            return "multipart";
        }

        // 非法枚举值。
        return {};
    }

    /*
     * 使用 hiredis 的 argv 形式执行 Redis 命令。
     *
     * 每个 std::string 都作为一个独立 Redis 参数发送，
     * 即使文件名中包含空格，也不会被拆成多个参数。
     */
    RedisReplyMgr ExecuteRedisCommand(
        redisContext* connection,
        const std::vector<std::string>& arguments)
    {
        if (connection == nullptr ||
            arguments.empty())
        {
            return RedisReplyMgr{};
        }

        std::vector<const char*> argumentPointers;
        std::vector<std::size_t> argumentLengths;

        argumentPointers.reserve(arguments.size());
        argumentLengths.reserve(arguments.size());

        for (const std::string& argument : arguments)
        {
            argumentPointers.push_back(
                argument.data()
            );

            argumentLengths.push_back(
                argument.size()
            );
        }

        auto* rawReply =
            reinterpret_cast<redisReply*>(
                redisCommandArgv(
                    connection,
                    static_cast<int>(
                        arguments.size()
                        ),
                    argumentPointers.data(),
                    argumentLengths.data()
                )
                );

        /*
         * RedisReplyMgr 获得 redisReply 所有权。
         * 函数返回后由 RedisReplyMgr 自动调用 freeReplyObject。
         */
        return RedisReplyMgr{ rawReply };
    }


    const std::string kCreateUploadSessionScript = R"lua(
local ttl = tonumber(ARGV[14])

if not ttl or ttl <= 0 then
    return -1
end

if redis.call("EXISTS", KEYS[1]) == 1 then
    return 0
end

redis.call(
    "HSET",
    KEYS[1],

    "session_id", ARGV[1],
    "object_key", ARGV[2],
    "client_file_id", ARGV[3],
    "uploader_id", ARGV[4],
    "conversation_id", ARGV[5],
    "original_file_name", ARGV[6],
    "expected_content_type", ARGV[7],
    "expected_size_bytes", ARGV[8],
    "expected_sha256", ARGV[9],
    "purpose", ARGV[10],
    "upload_mode", ARGV[11],
    "status", "pending",
    "failure_reason","",
    "expires_at_ms", ARGV[12],
    "created_at_ms", ARGV[13]
)

redis.call(
    "EXPIRE",
    KEYS[1],
    ttl
)

return 1
)lua";


    /*
     * 将上传会话从 pending 原子地转换为 ready。
     *
     * KEYS[1]：上传会话 Redis key。
     * ARGV[1]：MinIO 返回的实际 Content-Type。
     * ARGV[2]：MinIO 返回的实际文件大小。
     * ARGV[3]：MinIO 返回的 ETag，第一版允许为空。
     * ARGV[4]：完成记录的保留时间，单位为秒。
     *
     * 返回值：
     *   1  首次完成 pending -> ready；
     *   2  会话已经是内容相同的 ready，属于幂等重试；
     *   0  当前状态不允许转换，或者 ready 数据与本次请求冲突；
     *  -1  参数不合法；
     *  -2  会话不存在；
     *  -3  Redis 中的会话状态或完成数据损坏。
     */
    const std::string kMarkUploadReadyScript = R"lua(
local actual_content_type = ARGV[1]
local actual_size_bytes = tonumber(ARGV[2])
local etag = ARGV[3]
local retention_ttl = tonumber(ARGV[4])

-- 实际大小和 TTL 必须是正整数；ETag 第一版允许为空。
if not actual_content_type or
   actual_content_type == "" or
   not actual_size_bytes or
   actual_size_bytes <= 0 or
   actual_size_bytes ~= math.floor(actual_size_bytes) or
   not etag or
   not retention_ttl or
   retention_ttl <= 0 or
   retention_ttl ~= math.floor(retention_ttl) then
    return -1
end

if redis.call("EXISTS", KEYS[1]) == 0 then
    return -2
end

local status = redis.call("HGET", KEYS[1], "status")

-- 会话存在却没有状态，说明 Redis 中的数据结构已经损坏。
if not status then
    return -3
end

if status == "ready" then
    local completed = redis.call(
        "HMGET",
        KEYS[1],
        "actual_content_type",
        "actual_size_bytes",
        "etag"
    )

    -- ready 状态必须已经保存完整的实际对象信息。
    if not completed[1] or
       not completed[2] or
       not completed[3] then
        return -3
    end

    -- 相同的完成请求属于幂等重试，不重复写入，也不延长 TTL。
    if completed[1] == ARGV[1] and
       completed[2] == ARGV[2] and
       completed[3] == ARGV[3] then
        return 2
    end

    -- 同一个会话已经被另一组对象信息完成，拒绝覆盖。
    return 0
end

-- failed 是合法的终态，但不允许再转换为 ready。
if status == "failed" then
    return 0
end

-- 除 pending、ready、failed 之外的状态表示会话数据损坏。
if status ~= "pending" then
    return -3
end

redis.call(
    "HSET",
    KEYS[1],
    "status", "ready",
    "actual_content_type", ARGV[1],
    "actual_size_bytes", ARGV[2],
    "etag", ARGV[3]
)

redis.call(
    "EXPIRE",
    KEYS[1],
    retention_ttl
)

return 1
)lua";
}

// get用到的
namespace
{

    /*
 * 将 Redis 中保存的稳定失败原因字符串
 * 转换成 C++ 枚举。
 */
    std::optional<UploadFailureReason>
        ParseUploadFailureReason(
            std::string_view text) noexcept
    {
        if (text == "object_not_found")
        {
            return UploadFailureReason::ObjectNotFound;
        }

        if (text == "size_mismatch")
        {
            return UploadFailureReason::SizeMismatch;
        }

        if (text == "content_type_mismatch")
        {
            return UploadFailureReason::ContentTypeMismatch;
        }

        if (text == "etag_mismatch")
        {
            return UploadFailureReason::EtagMismatch;
        }

        if (text == "upload_expired")
        {
            return UploadFailureReason::UploadExpired;
        }

        return std::nullopt;
    }

	// 解析上传模式字符串为枚举值。
    std::optional<UploadMode> ParseUploadMode(
        std::string_view text) noexcept
    {
        if(text == "single_put")
			return UploadMode::SinglePut;
		if (text == "multipart")
			return UploadMode::Multipart;
		return std::nullopt;
    }

	// 解析上传会话状态字符串为枚举值。
    std::optional<UploadSessionStatus> ParseUploadSessionStatus(
        std::string_view text) noexcept
    {
        if (text == "pending")
            return UploadSessionStatus::Pending;
        if (text == "ready")
            return UploadSessionStatus::Ready;
        if (text == "failed")
            return UploadSessionStatus::Failed;
		return std::nullopt;
    }

	// 解析字符串为整数，失败返回 std::nullopt。
	template <typename Integer>
    bool ParseInteger(
        std::string_view text,
        Integer& Value) noexcept
    {
        if(text.empty())
			return false;

		const char* begin = text.data();
		const char* end = text.data() + text.size();

        const auto [position,error] =
            std::from_chars(
                begin,
                end,
                Value
			);

        return error == std::errc{} &&
			position == end;
    }
}

namespace
{
    /*
 * 将 C++ 失败原因枚举转换成稳定的 Redis 字符串。
 *
 * Redis 中不直接保存枚举整数，避免以后调整枚举顺序后，
 * 已有 Redis 数据的含义发生变化。
 */
    std::string_view UploadFailureReasonToText(
        UploadFailureReason reason) noexcept
    {
        switch (reason)
        {
        case UploadFailureReason::ObjectNotFound:
            return "object_not_found";

        case UploadFailureReason::SizeMismatch:
            return "size_mismatch";

        case UploadFailureReason::ContentTypeMismatch:
            return "content_type_mismatch";

        case UploadFailureReason::EtagMismatch:
            return "etag_mismatch";

        case UploadFailureReason::UploadExpired:
            return "upload_expired";
        }

        // 防止调用者传入非法枚举值。
        return {};
    }

    /*
 * 将上传会话从 pending 原子地转换为 failed。
 *
 * KEYS[1]：上传会话 Redis key。
 *
 * ARGV[1]：稳定的失败原因字符串。
 * ARGV[2]：失败记录的保留时间，单位为秒。
 *
 * 返回值：
 *   1  首次完成 pending -> failed；
 *   2  已经是相同原因的 failed，属于幂等重试；
 *   0  当前状态不允许转换，或者失败原因发生冲突；
 *  -1  参数不合法；
 *  -2  上传会话不存在；
 *  -3  Redis 中的会话状态或失败数据损坏。
 */
    const std::string kMarkUploadFailedScript = R"lua(
local failure_reason = ARGV[1]
local retention_ttl = tonumber(ARGV[2])

-- 失败原因不能为空，TTL 必须是正整数。
if not failure_reason or
   failure_reason == "" or
   not retention_ttl or
   retention_ttl <= 0 or
   retention_ttl ~= math.floor(retention_ttl) then
    return -1
end

-- 会话可能已经过期或被删除。
if redis.call("EXISTS", KEYS[1]) == 0 then
    return -2
end

local status = redis.call(
    "HGET",
    KEYS[1],
    "status"
)

-- key 存在却没有状态，说明 Hash 数据结构损坏。
if not status then
    return -3
end

if status == "failed" then
    local stored_failure_reason = redis.call(
        "HGET",
        KEYS[1],
        "failure_reason"
    )

    -- failed 状态必须包含稳定的失败原因。
    if not stored_failure_reason then
        return -3
    end

    -- 相同失败原因属于幂等重试。
    -- 不重复写入，也不延长 TTL。
    if stored_failure_reason == failure_reason then
        return 2
    end

    -- 同一个会话已经记录了其他失败原因，拒绝覆盖。
    return 0
end

-- ready 是成功终态，不能再被修改成 failed。
if status == "ready" then
    return 0
end

-- 除 pending、ready、failed 之外的状态属于损坏数据。
if status ~= "pending" then
    return -3
end

-- 首次将 pending 原子地修改为 failed。
redis.call(
    "HSET",
    KEYS[1],
    "status", "failed",
    "failure_reason", failure_reason
)

-- 将 TTL 更新为失败记录的保留时间。
redis.call(
    "EXPIRE",
    KEYS[1],
    retention_ttl
)

return 1
)lua";

}


RedisFileResult RedisFileMgr::CreateUploadSession(const UploadSession& session, std::chrono::seconds ttl)
{
    const std::string_view uploadText =
        UploadModeToText(
            session.upload_mode
        );

    /*
     * 创建上传会话时，只允许 pending 状态。
     *
     * ready 和 failed 必须由完成校验流程产生，
     * 不能在创建会话时由调用方指定。
     */
    if (session.session_id.empty() ||
        session.object_key.empty() ||
        session.uploader_id == 0 ||
        session.original_file_name.empty() ||
        session.expected_content_type.empty() ||
        session.expected_size_bytes == 0 ||
        session.created_at_ms <= 0 ||
        session.expires_at_ms <=
            session.created_at_ms ||
        session.status !=
            UploadSessionStatus::Pending ||
        session.failure_reason.has_value() ||
        uploadText.empty() ||
        ttl.count() <= 0)
    {
        return RedisFileResult::InvalidData;
    }

    if (session.session_id.find_first_of("{}") !=
        string::npos)
    {
        return RedisFileResult::InvalidData;
    }

    const int purposeValue =
        static_cast<int>(
            session.purpose);

    // 检查是否是定义的枚举值,并且拒绝unspecified
    if (!fileserver::v1::FilePurpose_IsValid(purposeValue) ||
        session.purpose == fileserver::v1::FILE_PURPOSE_UNSPECIFIED
    )
    {
        return RedisFileResult::InvalidData;
    }

    const string redisKey =
        MakeUploadSessionKey(
            session.session_id
        );

    // 把所有整数转换成string
    const string uploaderIdText =
        std::to_string(
            session.uploader_id
        );

    const string conversationIdText =
        std::to_string(
            session.conversation_id
        );

    const std::string expectedSizeText =
        std::to_string(
            session.expected_size_bytes
        );

    const std::string purposeText =
        std::to_string(
            purposeValue
        );

    const std::string expiresAtText =
        std::to_string(
            session.expires_at_ms
        );

    const std::string createdAtText =
        std::to_string(
            session.created_at_ms
        );

    const std::string ttlText =
        std::to_string(
            ttl.count()
        );

    // EVAL script key argv
    const std::vector<string> arguments{
        "EVAL",
        kCreateUploadSessionScript,
        "1",
        redisKey,
        session.session_id,
        session.object_key,
        session.client_file_id,
        uploaderIdText,
        conversationIdText,
        session.original_file_name,
        session.expected_content_type,
        expectedSizeText,
         session.expected_sha256,
        purposeText,
        std::string{uploadText},
        expiresAtText,
        createdAtText,
        ttlText
    };

    // 获取连接
    RedisConGuard guard{
        RedisPool::GetInstance()->
            BorrowConnect()
    };

    auto* connection =
        guard.get();

    if (connection == nullptr)
    {
        std::cerr
            << "[RedisFileMgr] no available Redis connection"
            << std::endl;

        return RedisFileResult::RedisError;
    }


    RedisReplyMgr reply =
        ExecuteRedisCommand(
            connection,
            arguments
        );


    // 连接断开或者没收到完整响应
    if (!reply)
    {
        std::cerr
            << "[RedisFileMgr] CreateUploadSession "
            << "returned no reply"
            << std::endl;

        return RedisFileResult::RedisError;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr
            << "[RedisFileMgr] CreateUploadSession "
            "Lua error: "
            << (
                reply->str != nullptr
                ? reply->str
                : "unknown error"
                )
            << std::endl;

        return RedisFileResult::RedisError;
    }

     // Lua return number 会被 hiredis 表示为 INTEGER
    if (reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr
            << "[RedisFileMgr] unexpected "
            "CreateUploadSession reply type: "
            << reply->type
            << std::endl;

        return RedisFileResult::RedisError;
    }

    switch (reply->integer)
    {
    case 1:
        return RedisFileResult::Success;

    case 0:
        return RedisFileResult::AlreadyExists;

    case -1:
        return RedisFileResult::InvalidData;

    default :

        std::cerr
            << "[RedisFileMgr] unexpected Lua result: "
            << reply->integer
            << std::endl;

        return RedisFileResult::RedisError;

    }


}

UploadSessionResult RedisFileMgr::GetUploadSession(
    const std::string& sessionId)
{
    /*
     * sessionId 会被放进 Redis Cluster 的 hash tag：
     *
     * upload:session:{sessionId}
     *
     * 因此不允许 sessionId 自己携带花括号，
     * 否则可能改变 Redis Cluster 的 hash tag 范围。
     */
    if (sessionId.empty() ||
        sessionId.find_first_of("{}") != std::string::npos)
    {
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
    }

    const string redisKey =
        MakeUploadSessionKey(
            sessionId
		);

    /* 约定的参数顺序
    HMGET upload:session:{session_id}
    session_id
    object_key
    client_file_id
    uploader_id
    conversation_id
    original_file_name
    expected_content_type
    expected_size_bytes
    expected_sha256
    purpose
    upload_mode
    status
    expires_at_ms
    created_at_ms
    */
    const std::vector<std::string> arguments{
    "HMGET",
    redisKey,
    "session_id",  //field[0]
    "object_key",
    "client_file_id",
    "uploader_id",
    "conversation_id",
    "original_file_name",
    "expected_content_type",
    "expected_size_bytes",
    "expected_sha256",
    "purpose",
    "upload_mode",
    "status",
    "expires_at_ms",
    "created_at_ms",
    "failure_reason"           // fields[14]
    };

	// 获取连接
    RedisConGuard guard{
        RedisPool::GetInstance()->
            BorrowConnect()
    };

    auto* connection =
        guard.get();
    if (connection == nullptr)
    {
        std::cerr
            << "[RedisFileMgr] no available Redis connection"
            << std::endl;
        return UploadSessionResult{
            RedisFileResult::RedisError,
            std::nullopt
        };
    }

    RedisReplyMgr reply =
        ExecuteRedisCommand(
            connection,
            arguments
        );

    // 错误处理
    if (!reply)
    {
        std::cerr
            << "[RedisFileMgr] GetUploadSession "
            << "returned no reply"
            << std::endl;
        return UploadSessionResult{
            RedisFileResult::RedisError,
            std::nullopt
		};
    }

    if(reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr
            << "[RedisFileMgr] GetUploadSession "
            "Redis error: "
            << (
                reply->str != nullptr
                ? reply->str
                : "unknown error"
                )
            << std::endl;
        return UploadSessionResult{
            RedisFileResult::RedisError,
            std::nullopt
        };
	}

    if (reply->type != REDIS_REPLY_ARRAY)
    {
        std::cerr
            << "[RedisFileMgr] unexpected "
            "GetUploadSession reply type: "
            << reply->type
            << ", elements: "
            << reply->elements
            << std::endl;
        return UploadSessionResult{
            RedisFileResult::RedisError,
            std::nullopt
        };
    }

	// 检查返回的元素数量是否符合预期
	constexpr size_t expectedElements = 15;
    constexpr size_t failureReasonIndex = 14;


    if(reply->elements != expectedElements)
    {
        std::cerr
            << "[RedisFileMgr] unexpected "
            "GetUploadSession reply elements: "
            << reply->elements
            << ", expected: "
            << expectedElements
            << std::endl;
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
	}

	// 区分会话不存在和会话存在但字段缺失的情况
    std::array<
        std::string_view,
        expectedElements>
        fields{};

	bool allNil = true;
	bool anyRequiredNil = false;
	// 如果会话不存在，Redis 会返回一个数组，所有元素都是 nil。
    // 遍历所有数组
    for (std::size_t index = 0;
        index < expectedElements;
        ++index)
    {
		auto* const element = reply->element[index];

		// hiredis不会在element数组中放置nullptr，nil元素的type是REDIS_REPLY_NIL
        if (element == nullptr)
        {
            return UploadSessionResult{
                RedisFileResult::InvalidData,
                std::nullopt
            };

        }
        if (element->type == REDIS_REPLY_NIL)
        {
            /*
                * failure_reason 是可选字段，并且可能不存在于
                * 修改数据结构之前创建的旧 Session 中。
                */
            if (index == failureReasonIndex)
            {
                fields[index] =
                    std::string_view{};

                continue;
            }

            anyRequiredNil = true;
            continue;
        }

        // 出现了非 NIL 字段，因此当前结果不是“全部字段均不存在”。
        allNil = false;

        if(element->type != REDIS_REPLY_STRING)
        {
            std::cerr
                << "[RedisFileMgr] unexpected "
                "GetUploadSession reply element type: "
                << element->type
                << ", index: "
                << index
                << std::endl;
            return UploadSessionResult{
                RedisFileResult::InvalidData,
                std::nullopt
            };
		}
        /*
        *Redis 允许保存空字符串。
        *
        * client_file_id 和 expected_sha256 第一版都可以为空，
        * 所以不能使用 element->len == 0 判断字段损坏。
        */
        if (element->str == nullptr &&
            element->len > 0)
        {
                std::cerr
                << "[RedisFileMgr] unexpected "
                "GetUploadSession reply element string is nullptr, "
                "index: "
                << index
                << std::endl;
                return UploadSessionResult{
                    RedisFileResult::InvalidData,
                    std::nullopt
                };
        }
        if(element->str == nullptr)
			fields[index] = std::string_view{};
        else
        {
            fields[index] = std::string_view{
                element->str,
                static_cast<std::size_t>(
                    element->len
                )
			};
        }
    }

    // 完成遍历 检查
    if(allNil)
    {
        return UploadSessionResult{
            RedisFileResult::NotFound,
            std::nullopt
        };
	}

    if (anyRequiredNil)
    {
        std::cerr
            << "[RedisFileMgr] GetUploadSession "
            "reply contains nil elements, "
            "sessionId: "
            << sessionId
            << std::endl;
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
		};
    }

	UploadSession session{};
	int purposeValue = 0;

    const bool numbersValid =
        ParseInteger(
            fields[3],
            session.uploader_id
        ) &&
        ParseInteger(
            fields[4],
            session.conversation_id
        ) &&
        ParseInteger(
            fields[7],
            session.expected_size_bytes
        ) &&
        ParseInteger(
            fields[9],
            purposeValue
        ) &&
        ParseInteger(
            fields[12],
            session.expires_at_ms
        ) &&
        ParseInteger(
            fields[13],
            session.created_at_ms
        );

    if (!numbersValid)
    {
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
    }


    const std::optional<UploadMode> uploadMode =
        ParseUploadMode(fields[10]);

    const std::optional<UploadSessionStatus> status =
        ParseUploadSessionStatus(fields[11]);

    if (!uploadMode.has_value() ||
        !status.has_value())
    {
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
    }

    if(!fileserver::v1::FilePurpose_IsValid(
            purposeValue
        ) ||
        purposeValue ==
            fileserver::v1::
                FILE_PURPOSE_UNSPECIFIED)
    {
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
	}

    std::optional<UploadFailureReason>
        failureReason;

    if (!fields[failureReasonIndex].empty())
    {
        failureReason =
            ParseUploadFailureReason(
                fields[failureReasonIndex]
            );

        /*
         * 字段非空却无法转换成已知枚举，说明 Redis 中的数据损坏。
         * 空字段是 pending/ready 的正常表示，不能在这里判错。
         */
        if (!failureReason.has_value())
        {
            std::cerr
                << "[RedisFileMgr] invalid upload failure reason"
                << ", sessionId: "
                << sessionId
                << ", value: "
                << fields[failureReasonIndex]
                << std::endl;

            return UploadSessionResult{
                RedisFileResult::InvalidData,
                std::nullopt
            };
        }
    }

    /*
     * 状态和失败原因必须满足：
     *
     * failed        -> 必须有 failure_reason
     * pending/ready -> 必须没有 failure_reason
     *
     * 这里直接检查解析结果，不能检查 session 的默认值。
     */
    const bool isFailed =
        *status == UploadSessionStatus::Failed;

    const bool hasFailureReason =
        failureReason.has_value();

    if (isFailed != hasFailureReason)
    {
        std::cerr
            << "[RedisFileMgr] upload status and "
            << "failure reason are inconsistent"
            << ", sessionId: "
            << sessionId
            << ", status: "
            << fields[11]
            << ", failure_reason: "
            << fields[failureReasonIndex]
            << std::endl;

        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
    }


    session.purpose =
        static_cast<fileserver::v1::FilePurpose>(
            purposeValue
		);

	session.upload_mode = *uploadMode;
	session.status = *status;
    session.failure_reason = failureReason;

    session.session_id = std::string{ fields[0] };
    session.object_key = std::string{ fields[1] };
    session.client_file_id = std::string{ fields[2] };
    session.original_file_name = std::string{ fields[5] };
    session.expected_content_type = std::string{ fields[6] };
    session.expected_sha256 = std::string{ fields[8] };

	// 解析完成,进行业务数据校验
	if (session.session_id != sessionId || // sessionId不匹配
        session.object_key.empty() ||
        session.uploader_id == 0 ||
        session.original_file_name.empty() ||
        session.expected_content_type.empty() ||
        session.expected_size_bytes == 0 ||
        session.created_at_ms <= 0 ||
        session.expires_at_ms <= session.created_at_ms)
    {
        return UploadSessionResult{
            RedisFileResult::InvalidData,
            std::nullopt
        };
    }

    return UploadSessionResult{
        RedisFileResult::Success,
        std::move(session)
	};
}

RedisFileResult RedisFileMgr::MarkUploadReady(
    const UploadCompleteInfo& completeInfo,
    std::chrono::seconds retentionTtl)
{
    /*
      * session_id 会被放入 Redis Cluster 的 hash tag：
      *
      * upload:session:{session_id}
      *
      * 因此不允许 session_id 自己包含花括号，
      * 否则可能改变 Redis Cluster 的 hash slot 计算范围。
      *
      * ETag 第一版允许为空，因此这里不检查 etag.empty()。
      */

    if (completeInfo.actual_content_type.empty() ||
        completeInfo.session_id.empty() ||
        completeInfo.actual_size_bytes == 0 ||
        completeInfo.session_id.find_first_of("{}") !=
        std::string::npos ||
        retentionTtl.count() <= 0
        )
    {
        return RedisFileResult::InvalidData;
    }

    const string redisKey =
        MakeUploadSessionKey(
            completeInfo.session_id
        );

    const string sizeText =
        std::to_string(
            completeInfo.actual_size_bytes
        );

    const string retentionText =
        std::to_string(
            retentionTtl.count()
        );

    /*
         * Redis EVAL 命令格式：
         *
         * EVAL script numkeys key [arg ...]
         *
         * 这里 numkeys 为 1，因此参数对应关系为：
         *
         * KEYS[1] = redisKey
         *
         * ARGV[1] = actual_content_type
         * ARGV[2] = actual_size_bytes
         * ARGV[3] = etag
         * ARGV[4] = retention_ttl_seconds
         */
    const std::vector<std::string> arguments{
        "EVAL",
        kMarkUploadReadyScript,
        "1",
        redisKey,
        completeInfo.actual_content_type,
        sizeText,
        completeInfo.etag,
        retentionText
    };

    RedisConGuard guard{
        RedisPool::GetInstance()->BorrowConnect()
    };

    auto* redisConn = guard.get();

    if (!redisConn)
    {
        std::cerr
            << "[RedisFileMgr] MarkUploadReady "
            << "no available Redis connection"
            << std::endl;

        return RedisFileResult::RedisError;
    }

    RedisReplyMgr reply =
        ExecuteRedisCommand(
            redisConn,
            arguments
        );

      /*
        *reply 为空通常表示连接断开、命令发送失败，
        * 或者 Redis 没有返回完整响应。
        */
        if (!reply)
        {
            std::cerr
                << "[RedisFileMgr] MarkUploadReady "
                << "returned no reply"
                << std::endl;

            return RedisFileResult::RedisError;
        }

    /*
     * Redis 返回 ERROR，可能是 Lua 语法错误、
     * Redis key 类型不正确，或者脚本执行过程中发生错误。
     */
    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr
            << "[RedisFileMgr] MarkUploadReady "
            << "Lua error: "
            << (
                reply->str != nullptr
                ? reply->str
                : "unknown error"
                )
            << std::endl;

        return RedisFileResult::RedisError;
    }

    /*
     * Lua 的数字返回值在 hiredis 中应该表现为
     * REDIS_REPLY_INTEGER。
     */
    if (reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr
            << "[RedisFileMgr] unexpected "
            << "MarkUploadReady reply type: "
            << reply->type
            << std::endl;

        return RedisFileResult::RedisError;
    }

    /*
     * Lua 返回值映射：
     *
     *  1：首次完成 pending -> ready
     *  2：相同对象信息的幂等重试
     *  0：当前状态不允许转换，或者 ready 数据冲突
     * -1：参数不合法
     * -2：上传会话不存在或已经过期
     * -3：Redis 中的会话状态或完成字段损坏
     */
    switch (reply->integer)
    {
    case 1:
        // 首次完成上传。
        return RedisFileResult::Success;

    case 2:
        // 已经完成，并且本次信息与之前一致。
        return RedisFileResult::Success;

    case 0:
        // failed 不能变回 ready，或者已有 ready 数据发生冲突。
        return RedisFileResult::InvalidState;

    case -1:
        // Lua 收到的参数不符合约定。
        return RedisFileResult::InvalidData;

    case -2:
        // Redis 会话不存在，通常是 session_id 错误或会话过期。
        return RedisFileResult::NotFound;

    case -3:
        /*
         * Redis key 存在，但是其中的 status 或完成字段损坏。
         * 这与 GetUploadSession() 对损坏 Hash 的处理保持一致。
         */
        return RedisFileResult::InvalidData;

    default:
        std::cerr
            << "[RedisFileMgr] unexpected "
            << "MarkUploadReady Lua result: "
            << reply->integer
            << std::endl;

        return RedisFileResult::RedisError;
    }
}

RedisFileResult RedisFileMgr::MarkUploadFailed(
    const std::string& sessionId,
    UploadFailureReason reason,
    std::chrono::seconds retentionTtl)
{
    /*
     * sessionId 会被放进 Redis Cluster 的 hash tag：
     *
     * upload:session:{sessionId}
     *
     * 因此不允许 sessionId 自己包含花括号，
     * 否则可能改变 Redis Cluster 的 hash slot。
     */
    if (sessionId.empty() ||
        sessionId.find_first_of("{}") !=
        std::string::npos ||
        retentionTtl.count() <= 0)
    {
        return RedisFileResult::InvalidData;
    }

    /*
     * 将枚举转换成稳定字符串。
     *
     * 如果调用者通过 static_cast 传入了非法枚举值，
     * UploadFailureReasonToText() 会返回空字符串。
     */
    const std::string_view failureReasonText =
        UploadFailureReasonToText(reason);

    if (failureReasonText.empty())
    {
        return RedisFileResult::InvalidData;
    }

    /*
     * 统一使用 MakeUploadSessionKey() 生成 Redis key，
     * 保证所有上传会话接口使用相同格式。
     */
    const std::string redisKey =
        MakeUploadSessionKey(sessionId);

    /*
     * hiredis argv 接口使用字符串传递参数，
     * 因此把 TTL 秒数转换成十进制字符串。
     */
    const std::string retentionTtlText =
        std::to_string(
            retentionTtl.count()
        );

    /*
     * Redis EVAL 命令：
     *
     * EVAL script numkeys key [arg ...]
     *
     * 参数对应关系：
     *
     * KEYS[1] = redisKey
     *
     * ARGV[1] = failure_reason
     * ARGV[2] = retention_ttl_seconds
     */
    const std::vector<std::string> arguments{
        "EVAL",
        kMarkUploadFailedScript,
        "1",
        redisKey,
        std::string{ failureReasonText },
        retentionTtlText
    };

    /*
     * 从 Redis 连接池借出连接。
     *
     * RedisConGuard 析构时会自动把连接归还连接池，
     * 因此后面的任意 return 都不会泄漏连接。
     */
    RedisConGuard guard{
        RedisPool::GetInstance()->
            BorrowConnect()
    };

    auto* redisConn = guard.get();

    if (redisConn == nullptr)
    {
        std::cerr
            << "[RedisFileMgr] MarkUploadFailed "
            << "no available Redis connection"
            << std::endl;

        return RedisFileResult::RedisError;
    }

    /*
     * 执行 EVAL 命令。
     *
     * RedisReplyMgr 会获得 redisReply 所有权，
     * 函数结束时自动调用 freeReplyObject()。
     */
    RedisReplyMgr reply =
        ExecuteRedisCommand(
            redisConn,
            arguments
        );

    /*
     * 没有收到 Redis 响应，通常表示连接断开、
     * 命令发送失败，或者响应读取失败。
     */
    if (!reply)
    {
        std::cerr
            << "[RedisFileMgr] MarkUploadFailed "
            << "returned no reply"
            << std::endl;

        return RedisFileResult::RedisError;
    }

    /*
     * Redis 返回错误，可能是 Lua 语法错误、
     * Redis key 类型错误或脚本运行错误。
     */
    if (reply->type == REDIS_REPLY_ERROR)
    {
        std::cerr
            << "[RedisFileMgr] MarkUploadFailed "
            << "Lua error: "
            << (
                reply->str != nullptr
                ? reply->str
                : "unknown error"
                )
            << std::endl;

        return RedisFileResult::RedisError;
    }

    /*
     * Lua 的数字返回值在 hiredis 中应当表现为
     * REDIS_REPLY_INTEGER。
     */
    if (reply->type != REDIS_REPLY_INTEGER)
    {
        std::cerr
            << "[RedisFileMgr] unexpected "
            << "MarkUploadFailed reply type: "
            << reply->type
            << std::endl;

        return RedisFileResult::RedisError;
    }

    /*
     * Lua 返回值映射：
     *
     *  1：首次完成 pending -> failed
     *  2：相同失败原因的幂等重试
     *  0：状态冲突或失败原因冲突
     * -1：参数不合法
     * -2：上传会话不存在或已经过期
     * -3：Redis Hash 中的状态或失败信息损坏
     */
    switch (reply->integer)
    {
    case 1:
        // 首次把 pending 修改成 failed。
        return RedisFileResult::Success;

    case 2:
        // 已经是相同原因的 failed，属于幂等重试。
        return RedisFileResult::Success;

    case 0:
        /*
         * 可能是以下情况：
         *
         * ready -> failed
         * failed -> 另一个失败原因
         */
        return RedisFileResult::InvalidState;

    case -1:
        // Lua 参数不符合约定。
        return RedisFileResult::InvalidData;

    case -2:
        // 上传会话不存在，通常是错误 sessionId 或会话过期。
        return RedisFileResult::NotFound;

    case -3:
        // Redis key 存在，但 Hash 中的状态或失败字段损坏。
        return RedisFileResult::InvalidData;

    default:
        std::cerr
            << "[RedisFileMgr] unexpected "
            << "MarkUploadFailed Lua result: "
            << reply->integer
            << std::endl;

        return RedisFileResult::RedisError;
    }
}
