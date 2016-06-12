#pragma once

class redis_rpc;

class redis_proxy_client : public acl::aio_callback
{
public:
	redis_proxy_client(acl::aio_socket_stream* conn, acl::redis_client_cluster* __manager);
	~redis_proxy_client();

private:
	virtual bool read_wakeup();
	virtual bool write_callback();
	virtual bool timeout_callback();
	virtual void close_callback();
private:
	acl::aio_socket_stream* conn_;
	redis_rpc* redis_;
	acl::redis_client_cluster* manager_;
};
