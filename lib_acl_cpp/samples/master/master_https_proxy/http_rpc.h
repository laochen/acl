#pragma once

class http_rpc : public acl::rpc_request
{
public:
	http_rpc(acl::aio_socket_stream* client, acl::http_request_manager* __conn_manager);
	~http_rpc();

protected:
	// 子线程处理函数
	virtual void rpc_run();

	// 主线程处理过程，收到子线程任务完成的消息
	virtual void rpc_onover();

private:
	acl::aio_socket_stream* client_;  // 客户端连接流
	acl::http_request_manager* conn_manager_;
	bool keep_alive_; // 是否与客户端保持长连接
	acl::dbuf_guard* dbuf_internal_;
	acl::dbuf_guard* dbuf_;

	// 在子线程中以阻塞方式处理客户端请求
	void handle_conn(acl::socket_stream* stream);
};
