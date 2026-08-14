#pragma once
#include "const.h"
#include "Singleton.h"

class Connection;

class ConnectionMgr:public Singleton<ConnectionMgr>
{
	friend class Singleton<ConnectionMgr>;
public:
	void AddConnection(std::shared_ptr<Connection> connection);
	void RmvConnection(const string& key);

private:
	ConnectionMgr();

private:
	std::shared_mutex mutex_;
	std::unordered_map<string, shared_ptr<Connection>> connections_map_;


};

