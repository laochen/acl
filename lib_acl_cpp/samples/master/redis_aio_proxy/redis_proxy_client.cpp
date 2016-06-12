#include "stdafx.h"
#include "rpc_manager.h"
#include "rpc_stats.h"
#include "redis_rpc.h"
#include "redis_proxy_client.h"

redis_proxy_client::redis_proxy_client(acl::aio_socket_stream* conn, acl::redis_client_cluster* __manager)
: conn_(conn)
, manager_(__manager)
{
	redis_ = new redis_rpc(conn_, manager_, var_cfg_keepalive);
}

redis_proxy_client::~redis_proxy_client()
{
	delete redis_;
}

bool redis_proxy_client::write_callback()
{
	return true;
}

bool redis_proxy_client::timeout_callback()
{
	return false;
}

void redis_proxy_client::close_callback()
{
	//logger("connection closed now, fd: %d", conn_->get_socket());
	delete this;
}

bool redis_proxy_client::read_wakeup()
{
	// 测试状态
	rpc_read_wait_del();
	rpc_add();

	// 先禁止异步流监控
	conn_->disable_read();

	// 发起一个 http 会话过程
	rpc_manager::get_instance().fork(redis_);

	return true;
}
