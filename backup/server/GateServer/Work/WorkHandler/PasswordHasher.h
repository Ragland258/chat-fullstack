#pragma once

#include <string>

class PasswordHasher
{
public:
    // 输入明文密码，返回哈希后的字符串
    static std::string HashPassword(
        const std::string& password);

    // 验证用户输入的密码是否正确
    static bool VerifyPassword(
        const std::string& password,
        const std::string& passwordHash);

private:
    // 初始化 libsodium
    static void InitSodium();
};