#include "MysqlMgr.h"

MysqlMgr::MysqlMgr()
    : dao_(MysqlPool::GetInstance())
{
}

ErrorCode MysqlMgr::RegisterUser(
    const std::string& name,
    const std::string& email,
    const std::string& passwordHash)
{
    const RegisterUserDbResult result =
        dao_.RegisterUser(
            name,
            email,
            passwordHash
        );

    switch (result)
    {
    case RegisterUserDbResult::Success:
        return ErrorCode::Success;

    case RegisterUserDbResult::UserAlreadyExists:
        return ErrorCode::User_Exist;

    case RegisterUserDbResult::PoolUnavailable:
        return ErrorCode::Mysql_Pool_Timeout;

    case RegisterUserDbResult::InvalidProcedureResult:
        return ErrorCode::Mysql_Result_Error;

    case RegisterUserDbResult::DatabaseError:
    default:
        return ErrorCode::Mysql_Error;
    }
}

std::string MysqlMgr::GetPasswordHash(const std::string& email)
{
    const UserPasswordHashQueryResult result =
		dao_.GetUserPasswordHash(email);
    switch (result.result)
    {
    case RegisterUserDbResult::Success:
			return result.passwordHash;
    default:
        return {};
    }
}
