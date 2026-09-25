#pragma once
#include "const.h"
#include "Singleton.h"
#include "generate/server.pb.h"



enum class UploadSessionStatus
{
    Pending,
    Ready,
    Failed
};

enum class UploadMode
{
    SinglePut,
    Multipart
};

enum class RedisFileResult
{
    Success,
    NotFound,
    AlreadyExists,
    InvalidData,
    InvalidState,
    RedisError
};


/*
 * 可以永久写入 Redis 的稳定失败原因。
 *
 * 不要直接把 exception.what() 写入 Redis，因为异常文本不稳定，
 * 可能包含内部实现信息，也不方便客户端和日志系统分类。
 */
enum class UploadFailureReason
{
    ObjectNotFound,
    SizeMismatch,
    ContentTypeMismatch,
    EtagMismatch,
    UploadExpired
};



struct UploadSession
{
    /*
     * 服务端生成的上传会话 ID。
     *
     * 一次上传尝试对应一个 session_id。
     * 同一个逻辑文件重新上传时，应生成新的 session_id。
     */
    std::string session_id;

    /*
     * MinIO 中的对象键。
     *
     * 例如：
     * avatar/3/uuid.png
     * chat/image/1001/uuid.webp
     * chat/file/1001/uuid.bin
     */
    std::string object_key;

    /*
     * 客户端生成的幂等 ID。
     *
     * 客户端因为网络问题重复 initUpload 时，可以使用这个字段
     * 找回已有会话，而不是重复创建多个对象。
     */
    std::string client_file_id;

    // 必须来自已认证的 Connection，不能相信客户端 JSON。
    std::uint64_t uploader_id{ 0 };

    /*
     * 文件所属的业务范围。
     *
     * 头像可以是 0；
     * 聊天文件可以保存 conversation_id。
     */
    std::uint64_t conversation_id{ 0 };

    /*
     * 原始文件名只作为显示信息。
     * 不能直接作为 MinIO object_key。
     */
    std::string original_file_name;

    // 申请上传时声明并经过 ChatServer 校验的 MIME 类型。
    std::string expected_content_type;

    // 申请上传时声明并经过 ChatServer 校验的字节数。
    std::uint64_t expected_size_bytes{ 0 };

    /*
     * 可选的内容校验和。
     *
     * 第一版可以为空，后面再支持 SHA-256。
     */
    std::string expected_sha256;

    // 头像、聊天图片、视频、附件等用途。
    fileserver::v1::FilePurpose purpose{
        fileserver::v1::FILE_PURPOSE_UNSPECIFIED
    };

    UploadMode upload_mode{
        UploadMode::SinglePut
    };

    UploadSessionStatus status{
        UploadSessionStatus::Pending
    };

    // FileServer 返回的预签名 URL 过期时间。
    std::int64_t expires_at_ms{ 0 };

    std::int64_t created_at_ms{ 0 };

    /*
     * 只有 failed 状态必须保存失败原因。
     * pending 和 ready 状态都应保持 std::nullopt。
     */
    std::optional<UploadFailureReason> failure_reason;
};


// 读取会话返回的结构体
struct UploadSessionResult
{
    RedisFileResult result{ RedisFileResult::RedisError};
    std::optional<UploadSession> session;
};

/*
 * FileServer 使用 MinIO StatObject 查询到的实际对象信息。
 *
 * ChatServer 已经将这些值与 UploadSession 中的预期值比较完成，
 * RedisFileMgr 只负责将最终结果原子写入 Redis。
 */
struct UploadCompleteInfo
{
    // 要完成的上传会话。
    std::string session_id;

    // MinIO 中对象的实际 MIME 类型。
    std::string actual_content_type;

    // MinIO 中对象的实际字节数。
    std::uint64_t actual_size_bytes{ 0 };

    // MinIO 返回的对象 ETag；第一版允许为空。
    std::string etag;
};

class RedisFileMgr final
    : public Singleton<RedisFileMgr>
{
    friend class Singleton<RedisFileMgr>;

public:

	RedisFileMgr(const RedisFileMgr&) = delete;

	RedisFileMgr& operator=(const RedisFileMgr&) = delete;

	~RedisFileMgr() = default;


    /*
     * 原子创建上传会话。
     *
     * Redis 中必须同时完成：
     * 1. 检查 session_id 对应的 Key 不存在；
     * 2. 写入完整 Hash；
     * 3. 设置整个 Hash 的 TTL。
     *
     * 已存在的会话不能被覆盖。
     */
    RedisFileResult CreateUploadSession(
        const UploadSession& session,
        std::chrono::seconds ttl
    );

    /*
     * 查询并解析完整上传会话。
     *
     * 会话不存在返回 NotFound；
     * Hash 字段缺失或格式错误返回 InvalidData；
     * Redis 命令执行失败返回 RedisError。
     */
    UploadSessionResult GetUploadSession(
        const std::string& sessionId
    );

    /*
     * 将会话从 pending 原子转换为 ready。
     *
     * 同时保存 MinIO 返回的实际大小、实际类型和 ETag，
     * 并将 TTL 更新为完成记录的保留时间。
     */
    RedisFileResult MarkUploadReady(
        const UploadCompleteInfo& completeInfo,
        std::chrono::seconds retentionTtl
    );

    /*
     * 将会话从 pending 原子转换为 failed。
     *
     * 同时保存稳定的失败原因，并设置失败记录的保留时间。
     * Redis、MySQL、FileServer 等临时故障不应调用该接口。
     */
    RedisFileResult MarkUploadFailed(
        const std::string& sessionId,
        UploadFailureReason reason,
        std::chrono::seconds retentionTtl
    );

private:

	RedisFileMgr() = default;


};
