#pragma once
#include "handler/RequestHandler.h"

class Connection;

class SetAvatarHandler final
	: public RequestHandler
{
	void Handler(
		const Json::Value& json,
		std::shared_ptr<Connection> connection
	) override;

	// 校验头像信息、申请预签名 URL，并创建 Redis 上传会话。
    void InitUpload(
        const Json::Value& data,
        const string& requestId,
        std::shared_ptr<Connection> connection
    );

	// 使用 session_id 校验已经上传到 MinIO 的头像对象。
    void CompleteUpload(
        const Json::Value& data,
        const string& requestId,
        std::shared_ptr<Connection> connection
    );
};


/* initPuload发包
{
    "version": 1,
        "require" : "setAvatar",
        "action" : "initUpload",
        "request_id" : "avatar-init-001",
        "data" : {
        "client_file_id": "550e8400-e29b-41d4-a716-446655440000",
            "size_bytes" : 358263,
            "content_type" : "image/png",
            "file_name" : "avatar.png"
    }
}
*/


/* initUpload回包
{
    "version": 1,
        "type" : "set_avatar_ack",
        "request_id" : "avatar-init-001",
        "error_code" : 0,
        "message" : "Upload Url Create",
        "data" : {
        "file_id": "avatar/3/83cbd209-b75c-4f24-bbcc-b12fa89940ff.png",
            "session_id" : "avatar/3/83cbd209-b75c-4f24-bbcc-b12fa89940ff.png",
            "upload_url" : "http://192.168.18.130:9100/...",
            "expires_at_ms" : 1788338396910,
            "upload_headers" : {
            "Content-Type": "image/png"
        }
    }
}
*/

/* 确认完成包
{
    "version": 1,
        "require" : "setAvatar",
        "action" : "completeUpload",
        "request_id" : "avatar-complete-001",
        "data" : {
        "session_id": "avatar/3/83cbd209-b75c-4f24-bbcc-b12fa89940ff.png"
    }
}
*/

/*
{
    "version": 1,
        "type" : "set_avatar_ack",
        "request_id" : "avatar-complete-retry-001",
        "error_code" : 0,
        "message" : "avatar upload already completed",
        "data" : {
        "action": "completeUpload",
            "session_id" : "avatar/3/83cbd209-b75c-4f24-bbcc-b12fa89940ff.png",
            "file_id" : "avatar/3/83cbd209-b75c-4f24-bbcc-b12fa89940ff.png",
            "status" : "ready"
    }
}
*/
