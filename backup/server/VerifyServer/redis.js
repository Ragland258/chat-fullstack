const redis = require('redis')
const config_module = require('./config')

let client = null
let readyPromise = null

function BuildClient() {
    const redisClient = redis.createClient({
        host: config_module.redis_host,
        port: config_module.redis_port,
        retry_strategy: (options) => {
            if (options.total_retry_time > 3000) {
                return new Error('redis retry time exhausted')
            }
            return Math.min(options.attempt * 100, 500)
        },
    })

    redisClient.on('error', (err) => {
        console.log('redis client error is ', err.message)
    })

    return redisClient
}

function AuthRedis(redisClient) {
    return new Promise((resolve, reject) => {
        if (!config_module.redis_passwd) {
            resolve()
            return
        }

        redisClient.ping((pingErr) => {
            if (!pingErr) {
                resolve()
                return
            }

            if (!pingErr.message || !pingErr.message.includes('NOAUTH')) {
                reject(pingErr)
                return
            }

            redisClient.auth(config_module.redis_passwd, (authErr) => {
                if (authErr) {
                    reject(authErr)
                    return
                }
                resolve()
            })
        })
    })
}

function WaitReady(redisClient) {
    return new Promise((resolve, reject) => {
        if (redisClient.ready) {
            resolve()
            return
        }

        redisClient.once('ready', resolve)
        redisClient.once('end', () => reject(new Error('redis connection ended before ready')))
    })
}

async function GetClient() {
    if (client && client.connected) {
        return client
    }

    if (!client) {
        client = BuildClient()
    }

    if (!readyPromise) {
        readyPromise = (async () => {
            await AuthRedis(client)
            await WaitReady(client)
            return client
        })().catch((err) => {
            readyPromise = null
            client = null
            throw err
        })
    }

    return await readyPromise
}

async function GetRedis(key) {
    const redisClient = await GetClient()
    return new Promise((resolve, reject) => {
        redisClient.get(key, (err, reply) => {
            if (err) {
                reject(err)
                return
            }
            resolve(reply)
        })
    })
}

async function SetRedisExpire(key, value, expireSeconds) {
    const redisClient = await GetClient()
    return new Promise((resolve, reject) => {
        redisClient.setex(key, expireSeconds, value, (err, reply) => {
            if (err) {
                reject(err)
                return
            }
            resolve(reply === 'OK')
        })
    })
}

async function QuitRedis() {
    if (!client) {
        return
    }

    const redisClient = client
    client = null
    readyPromise = null

    if (redisClient.connected) {
        redisClient.quit()
    }
}

module.exports = {
    GetRedis,
    SetRedisExpire,
    QuitRedis,
}
