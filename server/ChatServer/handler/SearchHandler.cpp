#include "SearchHandler.h"

#include "ioLoop/Connection.h"

/** search require json

{
  "version": 1,
  "require": "searchUser",
  "request_id": "search-001",
  "data": {
    "target_uid": "20002"
  }
}
*/

void SearchHandler::Handler(const Json::Value& data, std::shared_ptr<Connection> connection)
{
    if (!connection)
        return;

    // request必须存在且不能为空
	if (!data.isMember("request_id") ||
		data["request_id"].empty() ||
        data["request_id"].asString().empty())
	{
        Json::Value response = this->BuildJsonRsp(
            "search_user_ack",              // 响应类型
            "",                             // 请求没有合法 request_id
            ErrorCode::Message_Json_error,
            "request_id is missing or invalid"
        );
        connection->SendResponse(response.asString());
        return;
	}
    else
    {
        // target必须存在且不为空
        if (!data.isMember("data") ||
            !data["data"].isMember("target_id") ||
            data["data"]["target_id"].empty() ||
            data["data"]["target_id"].asString().empty())
        {
            Json::Value response = this->BuildJsonRsp(
                "search_user_ack",              // 响应类型
                data["request_id"].asString(),    // 请求没有合法 request_id
                ErrorCode::Message_Json_error,
                "target_id is missing or invalid"
            );
            connection->SendResponse(response.asString());
            return;
        }
        else
        {
            auto target_id = data["data"]["target_id"].asInt64();

            // 交给工作线程查找
            std::weak_ptr weakConn{ connection };
            ThreadPool::GetInstance()->commit(
                [weakConn,
                target_id]()
                {

                }
            );
        }
    }
}
