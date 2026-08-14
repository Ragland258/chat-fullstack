#include "PasswordHasher.h"

#include <sodium.h>

#include <stdexcept>


void PasswordHasher::InitSodium()
{
    // static 局部变量只会在第一次调用时初始化一次
    static const int result = sodium_init();

    if (result < 0)
    {
        throw std::runtime_error(
            "libsodium init failed"
        );
    }
}


std::string PasswordHasher::HashPassword(
    const std::string& password)
{
    InitSodium();

    if (password.empty())
    {
        throw std::invalid_argument(
            "password is empty"
        );
    }

    // libsodium 规定的哈希字符串缓冲区大小
    char passwordHash[crypto_pwhash_STRBYTES]{};

    const int result = crypto_pwhash_str(
        passwordHash,

        // 明文密码
        password.c_str(),

        // 明文密码长度
        static_cast<unsigned long long>(
            password.size()
            ),

        // 计算强度
        crypto_pwhash_OPSLIMIT_INTERACTIVE,

        // 内存消耗
        crypto_pwhash_MEMLIMIT_INTERACTIVE
    );

    if (result != 0)
    {
        throw std::runtime_error(
            "password hash failed"
        );
    }

    return std::string(passwordHash);
}


bool PasswordHasher::VerifyPassword(
    const std::string& password,
    const std::string& passwordHash)
{
    try
    {
        InitSodium();

        if (password.empty() ||
            passwordHash.empty())
        {
            return false;
        }

        const int result =
            crypto_pwhash_str_verify(
                // 数据库保存的密码哈希
                passwordHash.c_str(),

                // 用户本次输入的明文密码
                password.c_str(),

                // 明文密码长度
                static_cast<unsigned long long>(
                    password.size()
                    )
            );

        // 返回 0 表示密码正确
        return result == 0;
    }
    catch (...)
    {
        return false;
    }
}