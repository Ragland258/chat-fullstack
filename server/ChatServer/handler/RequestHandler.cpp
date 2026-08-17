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