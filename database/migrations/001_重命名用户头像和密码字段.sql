-- 迁移目标：统一 users 表中头像文件标识和密码哈希的字段语义。
--
-- 适用数据库：MySQL 8.0+
-- 前置结构：
--   users.avatar_url VARCHAR(512) NOT NULL DEFAULT ''
--   users.pwd        VARCHAR(255) NOT NULL
--
-- 注意：
-- 1. 本迁移只重命名字段，不重写字段值，已有用户数据会保留。
-- 2. 只能执行一次；执行前应先部署使用新字段名的服务程序。
-- 3. avatar_file_id 保存 MinIO 稳定对象键，不能保存会过期的预签名 URL。

ALTER TABLE users
    RENAME COLUMN avatar_url TO avatar_file_id,
    RENAME COLUMN pwd TO password_hash;
