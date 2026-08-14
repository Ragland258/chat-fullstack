#include "RegisterHandler.h"
#include "IoLoop/HttpConnection.h"
#include "ConfigMgr.h"
#include "Work/Redis/RedisMgr.h"
#include "PasswordHasher.h"
#include "Work/Mysql/MysqlMgr.h"
#include "Work/ThreadPool.h"



void RegisterHandler::Handler(std::shared_ptr<HttpConnection> connection)
{
    namespace http = boost::beast::http;

    //connection->DeferResponse();

    /*
     * request_ 只能在当前 IO 线程中读取。
     *
     * 先把请求体复制出来，再提交线程池。
     * 工作线程不能直接访问 connection->request_。
     */    
    std::string body =
        connection->GetRequest();

    /*
   * 后台任务不需要强制延长连接生命周期。
   * 客户端提前断开时，weak_ptr::lock() 会失败。
   */
    std::weak_ptr<HttpConnection> weakConn(connection);

    try
    {
        ThreadPool::GetInstance()->commit(
            [body = std::move(body), weakConn,this]()
            {
                //设置response的响应值
                http::status responseStatus =
                    http::status::ok;

                std::string responseJson;
                try
                {
                    Json::Reader reader;
                    Json::Value src_root;
                    bool parse_success = reader.parse(body, src_root);

                    if (!parse_success
                        || !src_root.isObject())
                    {
                        responseJson =
                            this->BuildJsonResponse(
                                    ErrorCode::Error_Json,
                                "invalid json"
                            );
                    }
                    else
                    {
                        //redis
                        const std::string email =
                            src_root.get(
                                "email",
                                ""
                            ).asString();

                        std::string user =
                            src_root.get(
                                "user",
                                ""
                            ).asString();

                        /*
                         * 兼容 password 和 passwd 两种字段。
                         */
                        const std::string password =
                            src_root.isMember("password")
                            ? src_root["password"].asString()
                            : src_root.get(
                                "passwd",
                                ""
                            ).asString();

                        const std::string confirmPassword =
                            src_root.get(
                                "confirm",
                                ""
                            ).asString();

                        /*
                         * 兼容新字段 verify_code
                         * 和旧字段 varifycode。
                         */
                        const std::string inputCode =
                            src_root.isMember("verify_code")
                            ? src_root["verify_code"].asString()
                            : src_root.get(
                                "varifycode",
                                ""
                            ).asString();

                        if (user.empty())
                        {
                            user = email;
                        }

                        if (email.empty()//数据丢失
                            || password.empty()
                            || inputCode.empty())
                        {
                            responseJson =
                                this->BuildJsonResponse(
                                        ErrorCode::Error_Json,
                                    "required field is missing"
                                );
                        }
                        else // 进行redis查询
                        {
                            const std::string redisKey =
                                "code_" + email;
                            const VerifyCodeResult verifyResult =
                                RedisMgr::GetInstance()->ConsumeVerifyCode(
                                    redisKey,
                                    inputCode
                                );

                            if (verifyResult == VerifyCodeResult::RedisError)
                            {
                                responseJson =
                                    this->BuildJsonResponse(
                                        ErrorCode::Unknown_Error,
                                        "redis service error"
                                    );
                            }
                            else if (verifyResult == VerifyCodeResult::CodeMissing)
                            {
                                responseJson =
                                    this->BuildJsonResponse(
                                        ErrorCode::Varify_Expired,
                                        "verify code expired or already used"
                                    );
                            }
                            else if (verifyResult == VerifyCodeResult::CodeMismatch)
                            {
                                responseJson =
                                    this->BuildJsonResponse(
                                        ErrorCode::Varify_Error,
                                        "verify code is incorrect"
                                    );
                            }
                            else
                            {
                                // 验证码正确，并且 Lua 已经将它删除
                                const std::string passwordHash =
                                    PasswordHasher::HashPassword(password);

                                const ErrorCode code =
                                    MysqlMgr::GetInstance()->RegisterUser(
                                        user,
                                        email,
                                        passwordHash
                                    );

                                switch (code)
                                {
                                case ErrorCode::Success:
                                    responseJson =
                                        this->BuildJsonResponse(
                                            code,
                                            "register success",
                                            email,
                                            user
                                        );
                                    break;

                                case ErrorCode::User_Exist:
                                    responseJson =
                                        this->BuildJsonResponse(
                                            code,
                                            "user already exists"
                                        );
                                    break;

                                case ErrorCode::Mysql_Pool_Timeout:
                                    responseJson =
                                        this->BuildJsonResponse(
                                            code,
                                            "database busy"
                                        );
                                    break;

                                default:
                                    responseJson =
                                        this->BuildJsonResponse(
                                            code,
                                            "database error"
                                        );
                                    break;
                                }
                            
                            }
                        }
                    }

                }
                catch (...)
                {
                    std::cerr
                        << "[register] unknown business exception"
                        << std::endl;

                    responseStatus =
                        http::status::internal_server_error;

                    responseJson =
                        this->BuildJsonResponse(
                            ErrorCode::Unknown_Error,
                            "internal server error"
                        );
                }

                auto lockedConnection =
                    weakConn.lock();

                if (!lockedConnection)
                {
                    /*
                     * 客户端已经断开，
                     * 不需要再发送响应。
                     */
                    return;
                }

                /*
                 * SendJsonResponse 内部会 post 回
                 * HttpConnection 的 Asio executor。
                 *
                 * 工作线程不直接修改 response_。
                 */
                lockedConnection->SendJsonResponse(
                    responseStatus,
                    std::move(responseJson)
                );
            });
    }
    catch (const std::exception& exception)
    {
        /*
         * commit 本身可能因为：
         *
         * 1. 线程池正在停止；
         * 2. 后续实现有界队列且队列已满；
         *
         * 而抛出异常。
         */
        std::cerr
            << "[register] submit task failed: "
            << exception.what()
            << std::endl;

        connection->SendJsonResponse(
            http::status::service_unavailable,
            BuildJsonResponse(
                 ErrorCode::Mysql_Pool_Timeout,
                "server busy"
            )
        );
    }
   
    
}
