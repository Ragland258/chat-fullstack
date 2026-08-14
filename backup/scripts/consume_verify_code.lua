-- 从 Redis 读取验证码
local storedCode = redis.call("GET", KEYS[1])

-- key 不存在，表示验证码过期或已被消费
if not storedCode then
    return 0
end

-- 用户输入的验证码不正确
if storedCode ~= ARGV[1] then
    return -1
end

-- 验证码正确，立即删除，保证只能使用一次
redis.call("DEL", KEYS[1])

return 1