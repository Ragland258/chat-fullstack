#pragma once
#include "const.h"
#include "Singleton.h"
#include "MysqlDao.h"
class MysqlMgr : public Singleton<MysqlMgr>
{
    friend class Singleton<MysqlMgr>;

public:
    ~MysqlMgr() = default;

    MysqlMgr(const MysqlMgr&) = delete;
    MysqlMgr& operator=(const MysqlMgr&) = delete;
    ErrorCode RegisterUser(const std::string& name, const std::string& email, const std::string& pwd);
	std::string GetPasswordHash(const std::string& email);
private:
    MysqlMgr();
    MysqlDao  dao_;
};