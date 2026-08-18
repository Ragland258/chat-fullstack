#include "minioMgr.h"

#include "ConfigMgr.h"

minioMgr::minioMgr()
    : base_url_( 
        (*ConfigMgr::GetInstance())
        ["Minio"]["Endpoint"]),             // minIO S3 API 地址
    provider_(
        (*ConfigMgr::GetInstance())
        ["Minio"]["AccessKey"],             // 账号
        (*ConfigMgr::GetInstance())
        ["Minio"]["SecretKey"]),            // 密码
    client_(base_url_, &provider_),
    bucket_(
        (*ConfigMgr::GetInstance())
        ["Minio"]["Bucket"])                // 当前使用的bucket
{
}