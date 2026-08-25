#pragma once
#include "const.h"
#include "Singleton.h"
#include "generate/server.grpc.pb.h"



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
};


class RedisFileMgr final
    : public Singleton<RedisFileMgr>
{

};
