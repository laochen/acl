#include "stdafx.h"
#include "rpc_stats.h"
#include "redis_parser.h"
#include "redis_rpc.h"

const char flags[] = {'-', '-', '-', '+', ':', '$', '*'};

static void error(acl::ostream &out, int id) {
    const char * info = redis_errno_description(id);
    out.write(info, strlen(info));
    return;
}


acl::string& redis_rpc::result_to_string(const acl::redis_result* result, acl::string& out)
{
    acl::redis_result_t type = result->get_type();
    if (type != acl::REDIS_RESULT_ARRAY)
    {
        acl::string buf;
        int len = result->argv_to_string(buf);
        if (len <= 0) {
            buf = "-1";
        }
        out += flags[type];
        if (type == acl::REDIS_RESULT_STRING) {
            out += len;
            out += "\r\n";
        }
        out += buf;
        out += "\r\n";
        return out;
    }

    size_t size;
    const acl::redis_result** children = result->get_children(&size);
    if (children == NULL) {
        return out;
    } else {
        out += flags[type];
        out += size;
        out += "\r\n";
    }

    for (size_t i = 0; i < size; i++)
    {
        const acl::redis_result* rr = children[i];
        if (rr != NULL) {
            result_to_string(rr, out);
        }
    }

    return out;
}

redis_rpc::redis_rpc(acl::aio_socket_stream* client, acl::redis_client_cluster* __manager,  int keepalive)
    : client_(client)
    , manager_(__manager)
    , keep_alive_(keepalive)
{
    //logger("rpc_request created!");
    dbuf_internal_ = new acl::dbuf_guard;
    dbuf_ = dbuf_internal_;

    redis_proxy_ = new acl::redis_proxy(manager_, manager_->size());
}

redis_rpc::~redis_rpc()
{
    if (redis_proxy_) {
        delete redis_proxy_;
    }

    if (dbuf_internal_) {
        delete dbuf_internal_;
    }
    //logger("rpc_request destroyed!");
}

// 调用 service_.rpc_fork 后，由 RPC 框架在子线程中调用本函数
// 来处理本地其它模块发来的请求信息
void redis_rpc::rpc_run()
{
    // 打开阻塞流对象
    acl::socket_stream stream;

    // 必须用 get_vstream() 获得的 ACL_VSTREAM 流对象做参数
    // 来打开 stream 对象，因为在 acl_cpp 和 acl 中的阻塞流
    // 和非阻塞流最终都是基于 ACL_VSTREAM，而 ACL_VSTREAM 流
    // 内部维护着了一个读/写缓冲区，所以在长连接的数据处理中，
    // 必须每次将 ACL_VSTREAM 做为内部流的缓冲流来对待
    ACL_VSTREAM* vstream = client_->get_vstream();
    ACL_VSTREAM_SET_RWTIMO(vstream, 10);
    (void) stream.open(vstream);
    // 设置为阻塞模式
    stream.set_tcp_non_blocking(false);

    if (var_cfg_rpc_stats_enabled) {
        rpc_req_add();
    }

    // 开始处理该 redis 请求
    handle_conn(&stream);

    if (var_cfg_rpc_stats_enabled) {
        rpc_req_del();
    }


    // 设置为非阻塞模式
    stream.set_tcp_non_blocking(true);

    // 将 ACL_VSTREAM 与阻塞流对象解绑定，这样才能保证当释放阻塞流对象时
    // 不会关闭与请求者的连接，因为该连接本身是属于非阻塞流对象的，需要采
    // 用异步流关闭方式进行关闭
    stream.unbind();
}

void redis_rpc::handle_conn(acl::socket_stream* stream)
{
    char  *buf;
    int   ret;
    long long wanted = 0;
    long long num_len = 24; //*12334534534534535353\r\n
    long long max = 0;
    std::vector<char *> fields;
    acl::string request;
    acl::ostream& out = *stream;

    buf = (char*) dbuf_->dbuf_alloc(num_len);
    buf[num_len - 1] = 0;

    ACL_VSTREAM* vstream = stream->get_vstream();
    ret = acl_vstream_gets_nonl(vstream, buf, sizeof(buf) - 1);
    if (ret == ACL_VSTREAM_EOF || ret < 2) {
        return error(out, 8);
    }
    request << buf << "\r\n";

    long long lines = acl_atoll(buf + 1);
    lines = lines * 2;

    if (lines <= 0) {
        return error(out, 6);
    }

    while (lines > 0) {
        max = wanted + 2 + 1 > num_len ? wanted + 2 + 1 : num_len; // 尾巴\r\n\0
        buf = (char*) dbuf_->dbuf_alloc(max);
        buf[max - 1] = 0;
        ret = acl_vstream_gets_nonl(vstream, buf, max);
        if (ret == ACL_VSTREAM_EOF) {
            return error(out, 8);
        }
        if ((lines & 1) == 0) {
            wanted =  acl_atoll(buf + 1);
        } else {
            //各字段内容
            if (ret != wanted) {
                return error(out, 8);
            }
            fields.push_back(buf);
            wanted = 0;
        }
        request << buf << "\r\n";
        lines = lines - 1;
    }

    int command_id =  check_command(fields);
    if (command_id < 0 ) {
        return error(out, 7);
    }

    int id = redisCommandTable[command_id].firstkey;
    redis_proxy_->hash_slot(fields[id], strlen(fields[id]));
    redis_proxy_->build_request(request.c_str(), request.length());
    const acl::redis_result* result = redis_proxy_->run();
    if (result) {
        acl::string outstr;
        result_to_string(result, outstr);
        out.write(outstr);
    } else {
        //服务器返回未知错误
        return error(out, 2);
    }
}

void redis_rpc::rpc_onover()
{
    if (var_cfg_rpc_stats_enabled) {
        // 减少 rpc 计数
        rpc_del();
    }

    if (keep_alive_)
    {
        if (var_cfg_rpc_stats_enabled) {
            rpc_read_wait_add();
        }

        // 监控异步流是否可读
        client_->read_wait(10);
    }
    else
        // 关闭异步流对象
        client_->close();
}