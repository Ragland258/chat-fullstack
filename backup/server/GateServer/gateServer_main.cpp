#include "const.h"
#include "IoLoop/Server.h"
#include "ConfigMgr.h"
#include "Work/Redis/RedisMgr.h"
#include <sodium.h>
void TestSodium()
{
	if (sodium_init() < 0)
	{
		std::cerr << "libsodium initialization failed\n";
		return;
	}

	std::cout << "libsodium initialization succeeded\n";
}

void TestRedisMgr()
{
	assert(RedisMgr::GetInstance()->Connect("127.0.0.1", 6379));
	//assert(RedisMgr::GetInstance()->Auth("123456"));
	assert(RedisMgr::GetInstance()->Set("blogwebsite", "llfc.club"));
	std::string value = "";
	assert(RedisMgr::GetInstance()->Get("blogwebsite", value));
	assert(RedisMgr::GetInstance()->Get("nonekey", value) == false);
	assert(RedisMgr::GetInstance()->HSet("bloginfo", "blogwebsite", "llfc.club"));
	assert(RedisMgr::GetInstance()->HGet("bloginfo", "blogwebsite") != "");
	assert(RedisMgr::GetInstance()->ExistsKey("bloginfo"));
	assert(RedisMgr::GetInstance()->Del("bloginfo"));
	assert(RedisMgr::GetInstance()->Del("bloginfo"));
	assert(RedisMgr::GetInstance()->ExistsKey("bloginfo") == false);
	assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue1"));
	assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue2"));
	assert(RedisMgr::GetInstance()->LPush("lpushkey1", "lpushvalue3"));
	assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
	assert(RedisMgr::GetInstance()->RPop("lpushkey1", value));
	assert(RedisMgr::GetInstance()->LPop("lpushkey1", value));
	assert(RedisMgr::GetInstance()->LPop("lpushkey2", value) == false);
	RedisMgr::GetInstance()->Close();
}

int main()
{
	try
	{

		auto config_mgr = ConfigMgr::GetInstance();

		auto gate_port = (*config_mgr)["GateServer"]["Port"];
		auto verify_host = (*config_mgr)["VarifyServer"]["Host"];
		auto verify_port = (*config_mgr)["VarifyServer"]["Port"];

		boost::asio::io_context ioc;
		boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);//生成信号集，监听SIGINT和SIGTERM信号
		signals.async_wait([&](const boost::system::error_code& error, int signal_number)//异步等待信号
			{
				if (error)
					return;
				ioc.stop();
			});
		unsigned short port = static_cast<unsigned short>(std::stoi(gate_port));
		// 加简单范围校验
		if (port > 65535 || port < 1)
		{
			std::cerr << "port out of range" << std::endl;
			return -2;
		}
		std::make_shared<Server>(ioc, port)->start();
		std::cout << "Server is running on port " << gate_port << std::endl;
		ioc.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}


