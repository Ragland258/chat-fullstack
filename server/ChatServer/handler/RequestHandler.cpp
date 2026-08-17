#include "handler/RequestHandler.h"

bool RequestHandler::ParseUint64(const Json::Value& value, std::uint64_t& result)
{
    result = 0;

    if (!value.isString())
        return false;

    const std::string text = value.asString();

    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        result);

    return error == std::errc{} &&
        end == text.data() + text.size() &&
        result != 0;
}
Json::Value RequestHandler::BuildJsonRsp(
    const std::string& type,
    const std::string& request_id,
    ErrorCode error_code,
    const std::string& message,
    Json::Value data ,
    int version )
{
    Json::Value root(Json::objectValue);

    root["version"] = version;
    root["type"] = type;

    if (!request_id.empty())
    {
        root["request_id"] = request_id;
    }

    root["error_code"] =
        static_cast<int>(error_code);

    root["message"] = message;

    if (!data.isNull())
    {
        root["data"] = std::move(data);
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";

    return Json::writeString(writer, root);
}