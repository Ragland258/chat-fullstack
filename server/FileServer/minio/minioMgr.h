#pragma once
#include "../const.h"
#include "../Singleton.h"

#include <miniocpp/client.h>
#include <optional>


class minioMgr: Singleton<minioMgr>
{
    friend class Singleton<minioMgr>;
public:
    minioMgr(minioMgr&) = delete;
    minioMgr& operator=(minioMgr&) = delete;
    ~minioMgr() = default;

private:
    minioMgr();


private:
    // provider_ 和 base_url_ 必须比 client_ 活得更久，成员顺序不要随意改变。
    minio::s3::BaseUrl base_url_;
    minio::creds::StaticProvider provider_;
    minio::s3::Client client_;                      //
    std::string bucket_;            //当前项目使用的bucket
};

