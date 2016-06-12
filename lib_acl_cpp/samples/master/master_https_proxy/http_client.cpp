#include "stdafx.h"
#include "rpc_manager.h"
#include "rpc_stats.h"
#include "http_rpc.h"
#include "http_client.h"

http_client::http_client(acl::aio_socket_stream* conn, acl::http_request_manager* __conn_manager)
: conn_(conn)
, conn_manager_(__conn_manager)
{
	http_ = new http_rpc(conn_, conn_manager_, (unsigned) var_cfg_content_length);
}

http_client::~http_client()
{
	delete http_;
}

bool http_client::write_callback()
{
	return true;
}

bool http_client::timeout_callback()
{
	return false;
}

void http_client::close_callback()
{
	//logger("connection closed now, fd: %d", conn_->get_socket());
	delete this;
}

bool http_client::read_wakeup()
{
	// 测试状态
	rpc_read_wait_del();
	rpc_add();

	// 先禁止异步流监控
	conn_->disable_read();

	// 发起一个 http 会话过程
	rpc_manager::get_instance().fork(http_);

	return true;
}
