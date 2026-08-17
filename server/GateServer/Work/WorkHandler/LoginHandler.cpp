#include "LoginHandler.h"

#include "IoLoop/HttpConnection.h"
#include "PasswordHasher.h"
#include "../Grpc/StatusGrpcClient.h"

#include <cstdint>
#include <string>

namespace
{
    bool ParseDeviceId(const Json::Value& value, std::uint64_t& deviceId)
    {
        deviceId = 0;

        try
        {
            if (value.isString())
            {
                const std::string text = value.asString();
                std::size_t parsedLength = 0;
                deviceId = std::stoull(text, &parsedLength);
                return parsedLength == text.size() && deviceId != 0;
            }

            if (value.isUInt64())
            {
                deviceId = value.asUInt64();
                return deviceId != 0;
            }
        }
        catch (const std::exception&)
        {
            deviceId = 0;
        }

        return false;
    }
}

void LoginHandler::Handler(std::shared_ptr<HttpConnection> connection)
{
    namespace http = boost::beast::http;

    std::weak_ptr<HttpConnection> weakConnection(connection);
    auto body = connection->GetRequest();

    try
    {
        ThreadPool::GetInstance()->commit(
            [body = std::move(body), weakConnection, this]()
            {
                http::status responseStatus = http::status::ok;
                std::string responseJson;

                try
                {
                    Json::Value requestJson;
                    Json::Reader reader;

                    if (!reader.parse(body, requestJson) || !requestJson.isObject())
                    {
                        responseJson = BuildJsonResponse(
                            ErrorCode::Error_Json,
                            "invalid json");
                    }
                    else
                    {
                        const std::string email = requestJson["email"].asString();
                        const std::string password = requestJson["password"].asString();
                        std::uint64_t deviceId = 0;

                        if (email.empty() || password.empty() ||
                            !ParseDeviceId(requestJson["device_id"], deviceId))
                        {
                            responseJson = BuildJsonResponse(
                                ErrorCode::Error_Json,
                                "email, password or device_id is invalid");
                        }
                        else
                        {
                            const UserLoginQueryResult loginInfo =
                                MysqlMgr::GetInstance()->GetLoginUserInfo(email);

                            if (loginInfo.result == RegisterUserDbResult::UserNotFound)
                            {
                                responseJson = BuildJsonResponse(
                                    ErrorCode::User_Not_Exist,
                                    "user not found");
                            }
                            else if (loginInfo.result != RegisterUserDbResult::Success ||
                                     loginInfo.uid == 0 || loginInfo.passwordHash.empty())
                            {
                                responseJson = BuildJsonResponse(
                                    ErrorCode::Mysql_Error,
                                    "mysql error");
                            }
                            else if (!PasswordHasher::VerifyPassword(
                                         password,
                                         loginInfo.passwordHash))
                            {
                                responseJson = BuildJsonResponse(
                                    ErrorCode::PassWord_Error,
                                    "password error");
                            }
                            else
                            {
                                const auto statusReply =
                                    StatusGrpcClient::GetInstance()->GetChatServer(
                                        loginInfo.uid,
                                        email,
                                        deviceId);

                                if (statusReply.error() !=
                                    static_cast<int>(ErrorCode::Success))
                                {
                                    responseJson = BuildJsonResponse(
                                        static_cast<ErrorCode>(statusReply.error()),
                                        "get chat server failed");
                                }
                                else if (statusReply.token().empty() ||
                                         statusReply.server_id().empty() ||
                                         statusReply.ip().empty() ||
                                         statusReply.port() <= 0)
                                {
                                    responseJson = BuildJsonResponse(
                                        ErrorCode::RPC_Error,
                                        "invalid status server response");
                                }
                                else
                                {
                                    Json::Value fields;
                                    fields["uid"] = std::to_string(loginInfo.uid);
                                    fields["device_id"] = std::to_string(deviceId);
                                    fields["email"] = email;
                                    fields["token"] = statusReply.token();
                                    fields["server_id"] = statusReply.server_id();
                                    fields["host"] = statusReply.ip();
                                    fields["port"] = std::to_string(statusReply.port());

                                    responseJson = BuildJsonResponse(
                                        ErrorCode::Success,
                                        "login success",
                                        fields);
                                }
                            }
                        }
                    }
                }
                catch (const std::exception& exception)
                {
                    std::cerr
                        << "[login] business exception: "
                        << exception.what()
                        << std::endl;

                    responseStatus = http::status::internal_server_error;
                    responseJson = BuildJsonResponse(
                        ErrorCode::Unknown_Error,
                        "internal server error");
                }

                const auto lockedConnection = weakConnection.lock();
                if (!lockedConnection)
                    return;

                lockedConnection->SendJsonResponse(
                    responseStatus,
                    std::move(responseJson));
            });
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "[login] submit task failed: "
            << exception.what()
            << std::endl;

        connection->SendJsonResponse(
            http::status::service_unavailable,
            BuildJsonResponse(
                ErrorCode::Mysql_Pool_Timeout,
                "server busy"));
    }
}
