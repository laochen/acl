#include "stdafx.h"
#include <assert.h>
#include "rpc_stats.h"
#include "rpc_timer.h"
#include "rpc_manager.h"
#include "redis_proxy_client.h"
#include "master_service.h"

static char *var_cfg_backend_addr;
static char *var_cfg_backend_pass;
static int var_cfg_redirect_max;
static int var_cfg_redirect_sleep;
static int var_cfg_max_conns;
static int var_cfg_conn_timeout;
static int var_cfg_rpc_timer_interval;

int var_cfg_rw_timeout;
char *var_cfg_rpc_addr;

acl::master_str_tbl var_conf_str_tab[] = {
	{ "redis_backend_addr", "10.105.17.224:6000", &var_cfg_backend_addr },
	{ "redis_backend_pass", "", &var_cfg_backend_pass },
	{ "rpc_addr", "127.0.0.1:0", &var_cfg_rpc_addr },
	{ 0, 0, 0 }
};

int   var_cfg_preread;
int   var_cfg_keepalive;
int   var_cfg_rpc_stats_enabled;

acl::master_bool_tbl var_conf_bool_tab[] = {
	{ "preread", 1, &var_cfg_preread },
	{ "keepalive", 1, &var_cfg_keepalive },
	{ "rpc_stats_enabled", 1, &var_cfg_rpc_stats_enabled },
	{ 0, 0, 0 }
};

int   var_cfg_nthreads_limit;
acl::master_int_tbl var_conf_int_tab[] = {
	{ "redis_redirect_max", 10, &var_cfg_redirect_max, 0, 0},
	{ "redis_redirect_sleep", 500, &var_cfg_redirect_sleep, 0, 0},
	{ "redis_max_conns", 100, &var_cfg_max_conns, 0, 0},
	{ "redis_conn_timeout", 10, &var_cfg_conn_timeout, 0, 0},
	{ "redis_rw_timeout", 10, &var_cfg_rw_timeout, 0, 0},
	{ "nthreads_limit", 4, &var_cfg_nthreads_limit, 0, 0 },
	{ "rpc_timer_interval", 10, &var_cfg_rpc_timer_interval, 0, 0 },
	{ 0, 0 , 0 , 0, 0 }
};


void setRedis(acl::redis_client_cluster* __manager) {
	__manager->set_retry_inter(1);
	// 设置重定向的最大阀值，若重定向次数超过此阀值则报错
	__manager->set_redirect_max(var_cfg_redirect_max);

	// 当重定向次数 >= 2 时每次再重定向此函数设置休息的时间(毫秒)
	__manager->set_redirect_sleep(var_cfg_redirect_sleep);

	acl::string buf(var_cfg_backend_addr);

	const std::vector<acl::string>& token = buf.split2(",; \t");
	__manager->set(var_cfg_backend_addr, var_cfg_max_conns, var_cfg_conn_timeout, var_cfg_rw_timeout);

	__manager->set_all_slot(token[0], var_cfg_max_conns);
	__manager->set_password("default", var_cfg_backend_pass);
}

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
	redis_proxy_client* callback = new redis_proxy_client(client, __manager);

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
	if (var_cfg_rpc_stats_enabled) {
		rpc_stats_init();
	}

	// get aio_handle from master_aio
	acl::aio_handle* handle = get_handle();
	assert(handle != NULL);

	// init rpc service
	rpc_manager::get_instance().init(handle, var_cfg_nthreads_limit,
	                                 var_cfg_rpc_addr);

	if (var_cfg_rpc_stats_enabled) {
		// start one timer to logger the rpc status
		rpc_timer* timer = new rpc_timer(*handle);
		timer->start(var_cfg_rpc_timer_interval);
	}


	__manager =  new acl::redis_client_cluster();
	setRedis(__manager);
}

void master_service::proc_on_exit()
{
	if (__manager) {
		delete __manager;
	}
	rpc_manager::get_instance().finish();
}
