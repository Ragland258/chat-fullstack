#pragma once
#include "const.h"
#include "Singleton.h"
#include "MysqlDao.h"

enum class UserRrofileFiled
{
    Avatar,
    NikeName,
    BackGround,
    Signature,

};

struct UserProfilePatch
{
    std::optional<std::string> nickname;

    std::optional<std::string> avatar_file_id;

    std::optional<std::string> background_file_id;

    std::optional<std::string> signature;

    bool Empty() const noexcept
    {
        return !nickname.has_value() &&
            !avatar_file_id.has_value() &&
            !background_file_id.has_value() &&
            !signature.has_value();
    }
};

struct UserProfileUploadResiult
{

};

class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;

public:
    ~MysqlMgr() = default;

    MysqlMgr(const MysqlMgr&) = delete;
    MysqlMgr& operator=(const MysqlMgr&) = delete;
    ErrorCode RegisterUser(
        const std::string& name,
        const std::string& email,
        const std::string& passwordHash);
	std::string GetPasswordHash(const std::string& email);

    Json::Value GetUserInfo(std::uint64_t uid);
private:
    MysqlMgr();
    MysqlDao  dao_;
};
