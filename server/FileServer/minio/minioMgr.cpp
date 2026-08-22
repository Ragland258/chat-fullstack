#include "minioMgr.h"

#include "../ConfigMgr.h"

#include <utility>

PresignedUrlResult minioMgr::CreateUploadUrl(
    const std::string& object_key,
    unsigned int expires_seconds)
{
    PresignedUrlResult result;

    if (!configured_)
    {
        result.error_message =
            "MinIO configuration is incomplete";
        return result;
    }

    if (object_key.empty())
    {
        result.error_message =
            "object key cannot be empty";
        return result;
    }

    minio::s3::GetPresignedObjectUrlArgs args;
    args.bucket = bucket_;
    args.object = object_key;
    args.method = minio::http::Method::kPut;
    args.expiry_seconds = expires_seconds;

    auto response =
        client_.GetPresignedObjectUrl(args);

    if (!response)
    {
        result.error_message =
            response.Error().String();
        return result;
    }

    result.success = true;
    result.url = std::move(response.url);
    return result;
}

PresignedUrlResult minioMgr::CreateDownloadUrl(
    const std::string& object_key,
    unsigned int expires_seconds)
{
    PresignedUrlResult result;

    if (!configured_)
    {
        result.error_message =
            "MinIO configuration is incomplete";
        return result;
    }

    if (object_key.empty())
    {
        result.error_message =
            "object key cannot be empty";
        return result;
    }

    minio::s3::GetPresignedObjectUrlArgs args;
    args.bucket = bucket_;
    args.object = object_key;
    args.method = minio::http::Method::kGet;
    args.expiry_seconds = expires_seconds;

    auto response =
        client_.GetPresignedObjectUrl(args);

    if (!response)
    {
        result.error_message =
            response.Error().String();
        return result;
    }

    result.success = true;
    result.url = std::move(response.url);
    return result;
}

ObjectStatResult minioMgr::StatObject(
    const std::string& object_key)
{
    ObjectStatResult result;

    if (!configured_)
    {
        result.error_message =
            "MinIO configuration is incomplete";
        return result;
    }

    if (object_key.empty())
    {
        result.error_message =
            "object key cannot be empty";
        return result;
    }

    minio::s3::StatObjectArgs args;
    args.bucket = bucket_;
    args.object = object_key;

    auto response =
        client_.StatObject(args);

    result.status_code = response.status_code;

    if (!response)
    {
        result.error_message =
            response.Error().String();
        return result;
    }

    result.success = true;
    result.size_bytes =
        static_cast<std::uint64_t>(response.size);
    result.etag = response.etag;
    result.content_type =
        response.headers.GetFront("content-type");

    const auto metadata_keys =
        response.user_metadata.Keys();

    for (const std::string& key : metadata_keys)
    {
        result.user_metadata.emplace(
            key,
            response.user_metadata.GetFront(key));
    }

    return result;
}

bool minioMgr::IsConfigured() const noexcept
{
    return configured_;
}

minioMgr::minioMgr()
    : base_url_(
          (*ConfigMgr::GetInstance())["Minio"]["Endpoint"],
          (*ConfigMgr::GetInstance())["Minio"]["Secure"] == "true")
    , provider_(
          (*ConfigMgr::GetInstance())["Minio"]["AccessKey"],
          (*ConfigMgr::GetInstance())["Minio"]["SecretKey"])
    , client_(base_url_, &provider_)
    , bucket_(
          (*ConfigMgr::GetInstance())["Minio"]["Bucket"])
{
    auto config =
        (*ConfigMgr::GetInstance())["Minio"];

    configured_ =
        static_cast<bool>(base_url_) &&
        !config["AccessKey"].empty() &&
        !config["SecretKey"].empty() &&
        !bucket_.empty();
}
