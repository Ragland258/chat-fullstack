#pragma once

#include "Singleton.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

#include <jdbc/mysql_driver.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/cppconn/exception.h>
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>

enum class RegisterUserDbResult
{
    Success,
	UserNotFound,
    UserAlreadyExists,
    PoolUnavailable,
    InvalidProcedureResult,
    DatabaseError
};

struct UserPasswordHashQueryResult
{
    RegisterUserDbResult result;
    std::string passwordHash;
};

struct SqlConnection
{
    using Clock = std::chrono::steady_clock;

    explicit SqlConnection(
        std::unique_ptr<sql::Connection> connection) noexcept
        : connection_(std::move(connection)),
          last_op_time_(Clock::now())
    {
    }

    SqlConnection(const SqlConnection&) = delete;
    SqlConnection& operator=(const SqlConnection&) = delete;

    SqlConnection(SqlConnection&&) noexcept = default;
    SqlConnection& operator=(SqlConnection&&) noexcept = default;

    std::unique_ptr<sql::Connection> connection_;
    Clock::time_point last_op_time_;
};

class MysqlPool : public Singleton<MysqlPool>
{
    friend class Singleton<MysqlPool>;

public:
    ~MysqlPool();

    MysqlPool(const MysqlPool&) = delete;
    MysqlPool& operator=(const MysqlPool&) = delete;

    std::unique_ptr<SqlConnection> BorrowConnection();

    void ReturnConnection(
        std::unique_ptr<SqlConnection> connection);

    void Stop();

private:
    MysqlPool();

    std::unique_ptr<SqlConnection> CreateConnection();
    void CheckConnection();
    void CheckLoop();
    void RefillConnections();

private:
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;

    std::size_t pool_size_{0};

    std::size_t connection_count_{0};

    std::queue<std::unique_ptr<SqlConnection>> pool_;

    std::mutex mutex_;
    std::condition_variable cv_;

    std::mutex check_mutex_;
    std::condition_variable check_cv_;

    std::atomic<bool> b_stop_{false};
    std::thread check_thread_;
};

class MysqlConnectionGuard
{
public:
    MysqlConnectionGuard(
        std::shared_ptr<MysqlPool> pool,
        std::unique_ptr<SqlConnection> connection) noexcept
        : pool_(std::move(pool)),
          connection_(std::move(connection))
    {
    }

    ~MysqlConnectionGuard() noexcept;

    MysqlConnectionGuard(const MysqlConnectionGuard&) = delete;
    MysqlConnectionGuard& operator=(const MysqlConnectionGuard&) = delete;

    MysqlConnectionGuard(MysqlConnectionGuard&&) noexcept = default;
    MysqlConnectionGuard& operator=(MysqlConnectionGuard&&) noexcept = delete;

    sql::Connection* Get() noexcept
    {
        return connection_ ? connection_->connection_.get() : nullptr;
    }

    const sql::Connection* Get() const noexcept
    {
        return connection_ ? connection_->connection_.get() : nullptr;
    }

    sql::Connection* operator->() noexcept
    {
        return Get();
    }

    const sql::Connection* operator->() const noexcept
    {
        return Get();
    }

    explicit operator bool() const noexcept
    {
        return Get() != nullptr;
    }


private:
    std::shared_ptr<MysqlPool> pool_;
    std::unique_ptr<SqlConnection> connection_;
};

class MysqlDao
{
public:
    explicit MysqlDao(std::shared_ptr<MysqlPool> pool);
    ~MysqlDao() = default;

    RegisterUserDbResult RegisterUser(
        const std::string& name,
        const std::string& email,
        const std::string& passwordHash);

    UserPasswordHashQueryResult GetUserPasswordHash(
		const std::string& email);

private:

    RegisterUserDbResult CheckUserExists(
        sql::Connection* connection,
		const std::string& email);

private:
    std::shared_ptr<MysqlPool> pool_;
};
