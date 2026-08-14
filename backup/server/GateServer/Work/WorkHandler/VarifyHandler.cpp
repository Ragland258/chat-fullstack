#include "VarifyHandler.h"

#include "../IoLoop/HttpConnection.h"
#include "../ThreadPool.h"
#include "Work/Grpc/VerifyGrpcClient.h"

#include <iostream>
#include <utility>
void VarifyHandler::Handler(
    std::shared_ptr<HttpConnection> connection)
{
    if (!connection)
    {
        return;
    }

    std::string body = connection->GetRequest();
    std::weak_ptr<HttpConnection> weakConn(connection);

    try
    {
        ThreadPool::GetInstance()->commit(
            [
                body = std::move(body),
                weakConn,
                this
            ]()
            {
                /*
                 * 请求已经到达业务 Handler。
                 * 业务结果统一通过 JSON error 字段表示。
                 */
                http::status responseStatus =
                    http::status::ok;

                std::string responseJson;

                try
                {
                    Json::Reader reader;
                    Json::Value requestJson;

                    const bool parseSuccess =
                        reader.parse(body, requestJson);

                    if (!parseSuccess
                        || !requestJson.isObject())
                    {
                        responseJson =
                            BuildJsonResponse(
                                ErrorCode::Error_Json,
                                "invalid json"
                            );
                    }
                    else if (
                        !requestJson.isMember("email")
                        || !requestJson["email"].isString()
                        || requestJson["email"]
                        .asString()
                        .empty())
                    {
                        responseJson =
                            BuildJsonResponse(
                                ErrorCode::Error_Json,
                                "email is required"
                            );
                    }
                    else
                    {
                        std::string email =
                            requestJson["email"].asString();

                        const GetVarifyRsp verifyResponse =
                            VarifyGrpcClient::GetInstance()
                            ->GetVerify(email);

                        const bool success =
                            verifyResponse.error()
                            == static_cast<int>(
                                ErrorCode::Success
                                );

                        if (!verifyResponse.email().empty())
                        {
                            email = verifyResponse.email();
                        }

                        if (success)
                        {
                            responseJson =
                                BuildJsonResponse(
                                    ErrorCode::Success,
                                    "验证码发送成功",
                                    email
                                );
                        }
                        else
                        {
                            /*
                             * 不返回 502。
                             * 前端通过 JSON error 判断失败。
                             */
                            responseJson =
                                BuildJsonResponse(
                                    ErrorCode::RPC_Error,
                                    "验证码服务调用失败",
                                    email
                                );

                            std::cerr
                                << "[get_verify] grpc failed, error: "
                                << verifyResponse.error()
                                << std::endl;
                        }
                    }
                }
                catch (const std::exception& exception)
                {
                    std::cerr
                        << "[get_verify] exception: "
                        << exception.what()
                        << std::endl;

                    /*
                     * 真正的服务器异常仍然返回 500。
                     */
                    responseStatus =
                        http::status::internal_server_error;

                    responseJson =
                        BuildJsonResponse(
                            ErrorCode::Unknown_Error,
                            "internal server error"
                        );
                }
                catch (...)
                {
                    responseStatus =
                        http::status::internal_server_error;

                    responseJson =
                        BuildJsonResponse(
                            ErrorCode::Unknown_Error,
                            "internal server error"
                        );
                }

                auto lockedConnection =
                    weakConn.lock();

                if (!lockedConnection)
                {
                    return;
                }

                lockedConnection->SendJsonResponse(
                    responseStatus,
                    std::move(responseJson)
                );
            }
        );
    }
    catch (const std::exception& exception)
    {
        std::cerr
            << "[get_verify] submit task failed: "
            << exception.what()
            << std::endl;

        connection->SendJsonResponse(
            http::status::service_unavailable,
            BuildJsonResponse(
                ErrorCode::Unknown_Error,
                "server busy"
            )
        );
    }
}