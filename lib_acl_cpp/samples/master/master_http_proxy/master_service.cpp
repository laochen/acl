#include "stdafx.h"
#include <assert.h>
#include "rpc_stats.h"
#include "rpc_timer.h"
#include "rpc_manager.h"
#include "http_client.h"
#include "master_service.h"

////////////////////////////////////////////////////////////////////////////////
// 配置内容项
char *var_cfg_rpc_addr;
char *var_cfg_backend_addr;
acl::master_str_tbl var_conf_str_tab[] = {
	{ "rpc_addr", "127.0.0.1:0", &var_cfg_rpc_addr },
	{ "http_backend_addr", "127.0.0.1:80", &var_cfg_backend_addr },
	{ 0, 0, 0 }
};

int   var_cfg_preread;
acl::master_bool_tbl var_conf_bool_tab[] = {
	{ "preread", 1, &var_cfg_preread },
	{ 0, 0, 0 }
};

int   var_cfg_max_conns;
int   var_cfg_conn_timeout;
int   var_cfg_rw_timeout;
int   var_cfg_nthreads_limit;
int   var_cfg_content_length;
acl::master_int_tbl var_conf_int_tab[] = {
	{ "nthreads_limit", 4, &var_cfg_nthreads_limit, 0, 0 },
	{ "content_length", 2048, &var_cfg_content_length, 0, 0 },
	{ "http_max_conns", 100, &var_cfg_max_conns, 0, 0 },
	{ "http_conn_timeout", 10, &var_cfg_conn_timeout, 0, 0 },
	{ "http_rw_timeout", 10, &var_cfg_rw_timeout, 0, 0 },
	{ 0, 0 , 0 , 0, 0 }
};

////////////////////////////////////////////////////////////////////////////////

master_service::master_service()
{
}

master_service::~master_service()
{
}

bool master_service::on_accept(acl::aio_socket_stream* client)
{
	// 如果允许在主线程中预读，则设置流的预读标志位
	if (var_cfg_preread)
	{
		ACL_VSTREAM* vstream = client->get_vstream();
		vstream->flag |= ACL_VSTREAM_FLAG_PREREAD;
	}

	// 创建异步客户端流的回调对象并与该异步流进行绑定
	http_client* callback = new http_client(client, __conn_manager);

	// 注册异步流的读回调过程
	client->add_read_callback(callback);

	// 注册异步流的写回调过程
	client->add_write_callback(callback);

	// 注册异步流的关闭回调过程
	client->add_close_callback(callback);

	// 注册异步流的超时回调过程
	client->add_timeout_callback(callback);

	rpc_read_wait_add();

	// 监控异步流是否可读
	client->read_wait(0);

	return true;
}

void master_service::proc_on_init()
{
	rpc_stats_init();

	// get aio_handle from master_aio
	acl::aio_handle* handle = get_handle();
	assert(handle != NULL);

	// init rpc service
	rpc_manager::get_instance().init(handle, var_cfg_nthreads_limit,
	                                 var_cfg_rpc_addr);

	// start one timer to logger the rpc status
	rpc_timer* timer = new rpc_timer(*handle);
	timer->start(1);

	__conn_manager = new acl::http_request_manager();
	__conn_manager->init(var_cfg_backend_addr, var_cfg_backend_addr, var_cfg_max_conns, var_cfg_conn_timeout, var_cfg_rw_timeout);
}

void master_service::proc_on_exit()
{
	if (__conn_manager) {
		// 销毁连接池
		delete __conn_manager;
	}
	rpc_manager::get_instance().finish();
}
