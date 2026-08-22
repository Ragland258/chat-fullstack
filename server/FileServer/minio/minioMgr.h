#pragma once

#include "../Singleton.h"

#include <cstdint>
#include <map>
#include <string>

#include <miniocpp/client.h>

/**
 * 预签名 URL 的生成结果。
 *
 * URL 使用 std::string 持有自己的内存，避免返回指向 SDK
 * 局部响应对象的悬空 std::string_view。
 */
struct PresignedUrlResult
{
    bool success{false};
    std::string url;
    std::string error_message;
};

/**
 * MinIO 对象元数据查询结果。
 *
 * CompleteUpload 使用这些信息确认对象真实存在，并校验实际大小、
 * 内容类型及上传时保存的用户元数据。
 */
struct ObjectStatResult
{
    bool success{false};

    // MinIO 返回的 HTTP 状态码，例如 200 或 404。
    int status_code{0};

    std::uint64_t size_bytes{0};
    std::string etag;
    std::string content_type;
    std::map<std::string, std::string> user_metadata;
    std::string error_message;
};

/**
 * MinIO 客户端管理器。
 *
 * FileServer 不转发文件二进制，只负责生成预签名 URL 和查询对象信息。
 */
class minioMgr : public Singleton<minioMgr>
{
    friend class Singleton<minioMgr>;

public:
    minioMgr(const minioMgr&) = delete;
    minioMgr& operator=(const minioMgr&) = delete;

    ~minioMgr() = default;

    // 生成短期有效的 HTTP PUT 上传地址。
    PresignedUrlResult CreateUploadUrl(
        const std::string& object_key,
        unsigned int expires_seconds);

    // 生成短期有效的 HTTP GET 下载地址。
    PresignedUrlResult CreateDownloadUrl(
        const std::string& object_key,
        unsigned int expires_seconds);

    // 使用 HTTP HEAD 查询对象，不下载文件内容。
    ObjectStatResult StatObject(
        const std::string& object_key);

    bool IsConfigured() const noexcept;

private:
    minioMgr();

private:
    /*
     * client_ 依赖 base_url_ 和 provider_，因此二者必须声明在
     * client_ 前面，并且生命周期覆盖 client_。
     */
    minio::s3::BaseUrl base_url_;
    minio::creds::StaticProvider provider_;
    minio::s3::Client client_;

    std::string bucket_;
    bool configured_{false};
};
