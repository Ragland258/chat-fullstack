#include "soloChatHandler.h"
#include "Message.h"

/** 当前json格式
* 
{
   "version": 1,
  "require": "soloChat",
  "request_id": "request-001",
  "data": {
    "conversation_id": "10001",
    "client_message_id": "550e8400-e29b-41d4-a716-446655440000",
    "message_type": 0,
    "payload": {
      "text": "你好"
    }
  }
}
*/


void soloChatHandler::Handler(
	const Json::Value& message,
	std::shared_ptr<Connection> connection)
{
    if (!connection)
        return;
    if (message.isNull() ||
        !message.isMember("request_id")) // 为空或者没有接收端id
    {

    }
}
