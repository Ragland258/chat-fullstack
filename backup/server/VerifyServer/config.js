const fs = require('fs');
const path = require('path');

let config_path = path.join(__dirname, 'config.json');
let config = JSON.parse(fs.readFileSync(config_path, 'utf8').replace(/^\uFEFF/, ''));
let email_user = config.email.user;
let email_pass = config.email.pass;
let mysql_host = config.mysql.host;
let mysql_port = config.mysql.port;
let redis_host = config.redis.host;
let redis_port = config.redis.port;
let redis_passwd = config.redis.passwd;
let redis_code_expire = config.redis.code_expire || 180;
let code_prefix = "code_";


module.exports = {email_pass, email_user, mysql_host, mysql_port,redis_host, redis_port, redis_passwd, redis_code_expire, code_prefix}
