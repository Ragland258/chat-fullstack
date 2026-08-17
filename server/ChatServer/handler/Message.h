#pragma once

#include "const.h"
enum class SendAckCode : std::uint16_t
{
    Stored,                 // 首次写入成功
    Duplicate,              // 已经写入过，本次是安全重试
    Unauthorized,           // 当前连接未认证或会话失效
    ConversationNotFound,   // 会话不存在
    NotConversationMember,  // 发送者不属于该会话
    InvalidMessage,         // 消息格式或大小不合法
    InternalError           // 服务端内部错误，可稍后重试
};

enum class MessageType : std::uint8_t
{
    Text,
    Image,
    Video,
    File,
    System
};

struct SendMessageCommand
{
    std::string request_id;
    std::string client_message_id;

    std::uint64_t conversation_id{ 0 };
    std::uint64_t sender_id{ 0 };
    std::uint64_t sender_device_id{ 0 };

    MessageType message_type{ MessageType::Text };
    Json::Value payload;
};


struct SendMessageAck
{
    std::string client_message_id;      // 对应客户端的哪次发送
    std::string message_id;             // 服务端正式消息 ID
    std::uint64_t conversation_id{ 0 };
    std::uint64_t conversation_seq{ 0 };  // 服务端分配的会话顺序
    SendAckCode code{ SendAckCode::InternalError };
    std::int64_t server_time_ms{ 0 };
    std::string error_message;
};

// 回执类型
enum class ReceiptType : std::uint8_t
{
    Store,      // 已持久化
    Delivered,  // 已送达设备
    Read        // 用户已阅读
};

struct ReceiptUpdate
{
    std::uint64_t conversation_id{ 0 };
    std::uint64_t through_seq{ 0 }; // 表示该序号及之前全部已送达或已读
    ReceiptType type{ ReceiptType::Delivered };
    std::uint64_t device_id{ 0 };
    std::int64_t client_time_ms{ 0 };
};

