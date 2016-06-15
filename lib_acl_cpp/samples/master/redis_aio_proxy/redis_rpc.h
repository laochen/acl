#pragma once

class redis_rpc : public acl::rpc_request
{
public:
	redis_rpc(acl::aio_socket_stream* client, acl::redis_client_cluster* __manager, int keepalive);
	~redis_rpc();

protected:
	// 子线程处理函数
	virtual void rpc_run();

	// 主线程处理过程，收到子线程任务完成的消息
	virtual void rpc_onover();

private:
	acl::aio_socket_stream* client_;  // 客户端连接流
	acl::redis_client_cluster* manager_;
	int keep_alive_; // 是否与客户端保持长连接
	char* res_buf_;  // 存放返回给客户端数据的缓冲区
	unsigned buf_size_; // res_buf_ 的空间大小
	acl::redis_proxy* redis_proxy_;
	acl::dbuf_guard* dbuf_internal_;
	acl::dbuf_guard* dbuf_;
	// 在子线程中以阻塞方式处理客户端请求
	void handle_conn(acl::socket_stream* stream);
	acl::string& result_to_string(const acl::redis_result* result, acl::string& out);
};
