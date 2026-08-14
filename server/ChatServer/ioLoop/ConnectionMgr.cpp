#include "ConnectionMgr.h"
#include "Connection.h"

ConnectionMgr::ConnectionMgr()
{
}

void ConnectionMgr::AddConnection(std::shared_ptr<Connection> connection)
{
	std::unique_lock<std::shared_mutex> lock(mutex_);
	connections_map_[connection->GetId()] = connection;
}

void ConnectionMgr::RmvConnection(const string& key)
{
	std::unique_lock<std::shared_mutex> lock(mutex_);
	connections_map_.erase(key);
}
