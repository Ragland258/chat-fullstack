const grpc = require('@grpc/grpc-js')
const { v4: uuidv4 } = require('uuid')
const message_proto = require('./proto')
const emailModule = require('./email')
const const_module = require('./const')
const config_module = require('./config')
const redis_module = require('./redis')

async function GetVarifyCode(call, callback) {
    console.log("email is ", call.request.email)
    try{
        const redisKey = const_module.code_prefix + call.request.email
        let query_res = await redis_module.GetRedis(redisKey)
        console.log("query_res is ", query_res)

        let uniqueId = query_res
        if (query_res == null) {
            uniqueId = uuidv4()
            if (uniqueId.length > 4) {
                uniqueId = uniqueId.substring(0, 4)
            }

            let bres = await redis_module.SetRedisExpire(redisKey, uniqueId, config_module.redis_code_expire)
            if (!bres) {
                callback(null, {
                    email: call.request.email,
                    error: const_module.Errors.RedisErr,
                    code: ""
                })
                return
            }
        }

        console.log("uniqueId is ", uniqueId)
        let text_str =  '您的验证码为'+ uniqueId +'请三分钟内完成注册'
        //发送邮件
        let mailOptions = {
            from: config_module.email_user,
            to: call.request.email,
            subject: '验证码',
            text: text_str,
        };

        let send_res = await emailModule.SendMail(mailOptions);
        console.log("send res is ", send_res)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Success,
            code: uniqueId
        });


    }catch(error){
        console.log("catch error is ", error)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Exception,
            code: ""
        });
    }

}

function main() {
    var server = new grpc.Server()
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    server.bindAsync('0.0.0.0:50051', grpc.ServerCredentials.createInsecure(), () => {
        console.log('grpc server started')
    })
}

main()
