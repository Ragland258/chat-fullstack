#include "MysqlDao.h"
#include "ConfigMgr.h"

#include <iostream>
#include <stdexcept>

namespace
{
    constexpr auto kCheckInterval = std::chrono::seconds(60);

    constexpr auto kIdleCheckTime = std::chrono::seconds(5);

    constexpr auto kBorrowTimeout = std::chrono::seconds(3);
}

MysqlPool::MysqlPool()
{
    auto config = ConfigMgr::GetInstance();

    const std::string host =
        (*config)["Mysql"]["Host"];

    const std::string port =
        (*config)["Mysql"]["Port"];

    user_ =
        (*config)["Mysql"]["User"];

    pass_ =
        (*config)["Mysql"]["Password"];

    schema_ =
        (*config)["Mysql"]["Schema"];

    std::string sizeText =
        (*config)["Mysql"]["PoolSize"];

    if (sizeText.empty())
    {
        sizeText =
            (*config)["Mysql"]["Size"];
    }

    if (host.empty() ||
        port.empty() ||
        user_.empty() ||
        schema_.empty() ||
        sizeText.empty())
    {
        throw std::runtime_error(
            "Mysql configuration is incomplete");
    }

    try
    {
        pool_size_ = static_cast<std::size_t>(
            std::stoul(sizeText));
    }
    catch (const std::exception&)
    {
        throw std::runtime_error(
            "Mysql PoolSize configuration is invalid");
    }

    if (pool_size_ == 0)
    {
        throw std::runtime_error(
            "Mysql connection pool size must be greater than zero");
    }

    if (host.rfind("tcp://", 0) == 0)
    {
        url_ = host + ":" + port;
    }
    else
    {
        url_ = "tcp://" + host + ":" + port;
    }

    try
    {
        for (std::size_t i = 0; i < pool_size_; ++i)
        {
            auto connection = CreateConnection();
            pool_.push(std::move(connection));
            ++connection_count_;
        }
    }
    catch (const sql::SQLException& e)
    {
        std::cerr
            << "MySQL pool initialization failed: "
            << e.what()
            << ", error code: "
            << e.getErrorCode()
            << ", SQL state: "
            << e.getSQLState()
            << '\n';

        throw;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "MySQL pool initialization failed: "
            << e.what()
            << '\n';

        throw;
    }

    check_thread_ = std::thread(
        &MysqlPool::CheckLoop,
        this);

    std::cout
        << "MySQL connection pool initialized, size: "
        << pool_size_
        << '\n';
}

MysqlPool::~MysqlPool()
{
    Stop();

    if (check_thread_.joinable())
    {
        check_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    while (!pool_.empty())
    {
        pool_.pop();
    }

    connection_count_ = 0;

    std::cout << "MySQL connection pool destroyed\n";
}

std::unique_ptr<SqlConnection> MysqlPool::CreateConnection()
{
    sql::Driver* driver =
        sql::mysql::get_driver_instance();

    if (driver == nullptr)
    {
        throw std::runtime_error(
            "failed to get MySQL driver instance");
    }

    std::unique_ptr<sql::Connection> connection(
        driver->connect(url_, user_, pass_));

    if (!connection)
    {
        throw std::runtime_error(
            "MySQL driver returned a null connection");
    }

    connection->setSchema(schema_);

    return std::make_unique<SqlConnection>(
        std::move(connection));
}

std::unique_ptr<SqlConnection> MysqlPool::BorrowConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);

    const bool ready = cv_.wait_for(
        lock,
        kBorrowTimeout,
        [this]()
        {
            return b_stop_.load() || !pool_.empty();
        });

    if (!ready || b_stop_.load())
    {
        return nullptr;
    }

    auto connection =
        std::move(pool_.front());

    pool_.pop();

    return connection;
}

void MysqlPool::ReturnConnection(
    std::unique_ptr<SqlConnection> connection)
{
    if (!connection)
    {
        return;
    }

    connection->last_op_time_ =
        SqlConnection::Clock::now();

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (b_stop_.load())
        {
            if (connection_count_ > 0)
            {
                --connection_count_;
            }

            return;
        }

        pool_.push(std::move(connection));
    }

    cv_.notify_one();
}

void MysqlPool::Stop()
{
    b_stop_.store(true);

    cv_.notify_all();
    check_cv_.notify_all();
}

void MysqlPool::CheckLoop()
{
    std::unique_lock<std::mutex> checkLock(
        check_mutex_);

    while (!b_stop_.load())
    {
        const bool stopped = check_cv_.wait_for(
            checkLock,
            kCheckInterval,
            [this]()
            {
                return b_stop_.load();
            });

        if (stopped || b_stop_.load())
        {
            break;
        }

        checkLock.unlock();

        try
        {
            CheckConnection();
            RefillConnections();
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "MySQL check thread error: "
                << e.what()
                << '\n';
        }
        catch (...)
        {
            std::cerr
                << "MySQL check thread unknown error\n";
        }

        checkLock.lock();
    }
}

void MysqlPool::CheckConnection()
{
    std::size_t idleConnectionCount = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        idleConnectionCount = pool_.size();
    }

    for (std::size_t i = 0;
         i < idleConnectionCount;
         ++i)
    {
        if (b_stop_.load())
        {
            return;
        }

        std::unique_ptr<SqlConnection> connection;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (pool_.empty())
            {
                break;
            }

            connection = std::move(pool_.front());
            pool_.pop();
        }

        if (!connection)
        {
            continue;
        }

        bool connectionValid = true;
        const auto now = SqlConnection::Clock::now();

        if (now - connection->last_op_time_ >=
            kIdleCheckTime)
        {
            try
            {
                if (!connection->connection_)
                {
                    throw std::runtime_error(
                        "MySQL connection pointer is null");
                }

                if (connection->connection_->isClosed())
                {
                    throw std::runtime_error(
                        "MySQL connection is closed");
                }

                std::unique_ptr<sql::Statement> statement(
                    connection->connection_->createStatement());

                statement->execute("SELECT 1");

                connection->last_op_time_ =
                    SqlConnection::Clock::now();
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "MySQL keep-alive failed: "
                    << e.what()
                    << '\n';

                try
                {
                    connection = CreateConnection();
                    std::cout << "MySQL connection rebuilt\n";
                }
                catch (const sql::SQLException& reconnectError)
                {
                    std::cerr
                        << "MySQL reconnect failed: "
                        << reconnectError.what()
                        << ", error code: "
                        << reconnectError.getErrorCode()
                        << ", SQL state: "
                        << reconnectError.getSQLState()
                        << '\n';

                    connectionValid = false;
                }
                catch (const std::exception& reconnectError)
                {
                    std::cerr
                        << "MySQL reconnect failed: "
                        << reconnectError.what()
                        << '\n';

                    connectionValid = false;
                }
                catch (...)
                {
                    std::cerr
                        << "MySQL reconnect failed: unknown error\n";

                    connectionValid = false;
                }
            }
        }

        bool returnedToPool = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (connectionValid &&
                connection &&
                !b_stop_.load())
            {
                pool_.push(std::move(connection));
                returnedToPool = true;
            }
            else if (connection_count_ > 0)
            {
                --connection_count_;
            }
        }

        if (returnedToPool)
        {
            cv_.notify_one();
        }
    }
}

void MysqlPool::RefillConnections()
{
    while (!b_stop_.load())
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (connection_count_ >= pool_size_)
            {
                return;
            }
        }

        try
        {
            auto connection = CreateConnection();

            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (b_stop_.load())
                {
                    return;
                }

                if (connection_count_ >= pool_size_)
                {
                    return;
                }

                pool_.push(std::move(connection));
                ++connection_count_;
            }

            cv_.notify_one();

            std::cout
                << "MySQL pool added a new connection\n";
        }
        catch (const sql::SQLException& e)
        {
            std::cerr
                << "Failed to refill MySQL connection: "
                << e.what()
                << ", error code: "
                << e.getErrorCode()
                << ", SQL state: "
                << e.getSQLState()
                << '\n';

            return;
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "Failed to refill MySQL connection: "
                << e.what()
                << '\n';

            return;
        }
    }
}

MysqlConnectionGuard::~MysqlConnectionGuard() noexcept
{
    if (!pool_ || !connection_)
    {
        return;
    }

    try
    {
        pool_->ReturnConnection(
            std::move(connection_));
    }
    catch (...)
    {
    }
}

MysqlDao::MysqlDao(std::shared_ptr<MysqlPool> pool)
    : pool_(std::move(pool))
{
    if (!pool_)
    {
        throw std::invalid_argument(
            "MysqlDao requires a valid MysqlPool");
    }
}


RegisterUserDbResult MysqlDao::RegisterUser(
    const std::string& name,
    const std::string& email,
    const std::string& passwordHash)
{
    MysqlConnectionGuard connection(
        pool_,
        pool_->BorrowConnection()
    );

    if (!connection)
    {
        return RegisterUserDbResult::PoolUnavailable;
    }

    const RegisterUserDbResult checkResult =
        CheckUserExists(connection.Get(), email);

    if (checkResult != RegisterUserDbResult::Success)
    {
        return checkResult;
    }

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(
            connection->prepareStatement(
                "INSERT INTO users(name, email, pwd) "
                "VALUES (?, ?, ?)"
            )
        );

        statement->setString(1, name);
        statement->setString(2, email);
        statement->setString(3, passwordHash);

        const int affectedRows =
            statement->executeUpdate();

        if (affectedRows != 1)
        {
            return RegisterUserDbResult::DatabaseError;
        }

        return RegisterUserDbResult::Success;
    }
    catch (const sql::SQLException& exception)
    {
        std::cerr
            << "RegisterUser SQL error: "
            << exception.what()
            << ", error code: "
            << exception.getErrorCode()
            << ", SQL state: "
            << exception.getSQLState()
            << '\n';

        // 防止并发注册造成唯一索引冲突
        if (exception.getErrorCode() == 1062)
        {
            return RegisterUserDbResult::UserAlreadyExists;
        }

        return RegisterUserDbResult::DatabaseError;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "RegisterUser error: "
            << exception.what()
            << '\n';

        return RegisterUserDbResult::DatabaseError;
    }
}

UserPasswordHashQueryResult MysqlDao::GetUserPasswordHash(const std::string& email)
{
    MysqlConnectionGuard connection(
        pool_,
        pool_->BorrowConnection()
    );

    if (!connection)
    {
        return {};
    }

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(
            connection->prepareStatement(
                "SELECT pwd "
                "FROM users "
                "WHERE email = ? "
                "LIMIT 1"
            )
        );

        statement->setString(1, email);

        std::unique_ptr<sql::ResultSet> result(
            statement->executeQuery()
        );

        if (!result->next())
        {
            // 邮箱对应的用户不存在
            return {RegisterUserDbResult::UserNotFound, ""};
        }

        return {RegisterUserDbResult::Success, result->getString("pwd").asStdString()};
    }
    catch (const sql::SQLException& exception)
    {
        std::cerr
            << "GetUserPasswordHash SQL error: "
            << exception.what()
            << ", error code: "
            << exception.getErrorCode()
            << ", SQL state: "
            << exception.getSQLState()
            << '\n';

        return {};
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "GetUserPasswordHash error: "
            << exception.what()
            << '\n';

        return {};
    }

}

RegisterUserDbResult MysqlDao::CheckUserExists(
    sql::Connection* connection,
    const std::string& email)
{
    if (connection == nullptr)
    {
        return RegisterUserDbResult::PoolUnavailable;
    }

    try
    {
        std::unique_ptr<sql::PreparedStatement> statement(
            connection->prepareStatement(
                "SELECT 1 "
                "FROM users "
                "WHERE email = ? "
                "LIMIT 1"
            )
        );

        statement->setString(1, email);

        std::unique_ptr<sql::ResultSet> result(
            statement->executeQuery()
        );

        if (result->next())
        {
            return RegisterUserDbResult::UserAlreadyExists;
        }

        return RegisterUserDbResult::Success;
    }
    catch (const sql::SQLException& exception)
    {
        std::cerr
            << "CheckUserExists SQL error: "
            << exception.what()
            << ", error code: "
            << exception.getErrorCode()
            << ", SQL state: "
            << exception.getSQLState()
            << '\n';

        return RegisterUserDbResult::DatabaseError;
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "CheckUserExists error: "
            << exception.what()
            << '\n';

        return RegisterUserDbResult::DatabaseError;
    }
}