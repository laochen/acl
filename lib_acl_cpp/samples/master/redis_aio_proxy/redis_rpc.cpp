#include "stdafx.h"
#include "rpc_stats.h"
#include "redis_rpc.h"
#include "redis_parse.h"

const char flags[] = {'-', '-', '-', '+', ':', '$', '*'};

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
    redis_proxy_ = new acl::redis_proxy(manager_, manager_->size());
}

redis_rpc::~redis_rpc()
{
    if (redis_proxy_) {
        // 销毁连接池
        delete redis_proxy_;
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

    rpc_req_add();

    // 开始处理该 HTTP 请求
    handle_conn(&stream);

    rpc_req_del();

    // 设置为非阻塞模式
    stream.set_tcp_non_blocking(true);

    // 将 ACL_VSTREAM 与阻塞流对象解绑定，这样才能保证当释放阻塞流对象时
    // 不会关闭与请求者的连接，因为该连接本身是属于非阻塞流对象的，需要采
    // 用异步流关闭方式进行关闭
    stream.unbind();
}

void redis_rpc::handle_conn(acl::socket_stream* stream)
{
    struct redis_parse_t redis_parse;
    unsigned len = 0, size = 0;
    unsigned parse_start = 0;
    acl::string request;
    char *p = NULL;
    char * data;
    char  buf[8192];
    int   ret;
    acl::ostream& out = *stream;

    ACL_VSTREAM* vstream = stream->get_vstream();
    vstream->rw_timeout = var_cfg_rw_timeout;
    ret = acl_vstream_gets_nonl(vstream, buf, sizeof(buf) - 1);
    if (ret == ACL_VSTREAM_EOF || ret < 2) {
        const char * error = redis_errno_description(2);
        out.write(error, strlen(error));
        return;
    }
    request = request << buf << "\r\n";

    long long lines = acl_atoll(buf + 1);
    lines = lines * 2;

    if (lines <= 0) {
        const char * error = redis_errno_description(2);
        out.write(error, strlen(error));
        return;
    }

    while (lines > 0) {
        ret = acl_vstream_gets_nonl(vstream, buf, sizeof(buf) - 1);
        if (ret == ACL_VSTREAM_EOF) {
            const char * error = redis_errno_description(2);
            out.write(error, strlen(error));
            return;
        }
        request = request << buf << "\r\n";
        lines = lines - 1;
    }

    data = request.c_str();
    len = request.length();
    size = len;

    redis_parse_init(&redis_parse, REDIS_REQUEST, (char *const *)&data, &size);
    do {
        redis_parse_request(&redis_parse);
        if (redis_parse.rs.over) {
            int id = redisCommandTable[redis_parse.rs.command_id].firstkey;
            redis_proxy_->hash_slot(data + redis_parse.rs.field[id].offset, redis_parse.rs.field[id].len);
            redis_proxy_->build_request(data + parse_start, redis_parse.rs.dosize);
            const acl::redis_result* result = redis_proxy_->run();
            if (result) {
                acl::string outstr;
                result_to_string(result, outstr);
                out.write(outstr);
            } else {
                //服务器返回未知错误
                const char * error = redis_errno_description(2);
                out.write(error, strlen(error));
            }
            parse_start = parse_start + redis_parse.rs.dosize;
            p = data + parse_start;
            size = len - parse_start;
            if (size > 0) {
                redis_parse_init(&redis_parse, REDIS_REQUEST, (char *const *)&p, &size);
            }
        } else {
            //返回解释错误
            const char * error = redis_errno_description(redis_parse.rs.error);
            out.write(error, strlen(error));

            //继续解释，找到起点
            parse_start = parse_start + redis_parse.rs.dosize;
            p = strchr(data + parse_start, '*');
            if (p) {
                parse_start = p - data;
                size = len - parse_start;
                if (size > 0) {
                    redis_parse_init(&redis_parse, REDIS_REQUEST, (char *const *)&p, &size);
                }
            } else {
                break;
            }
        }
    } while (parse_start < len);
}

void redis_rpc::rpc_onover()
{
    // 减少 rpc 计数
    rpc_del();

    if (keep_alive_)
    {
        rpc_read_wait_add();

        // 监控异步流是否可读
        client_->read_wait(10);
    }
    else
        // 关闭异步流对象
        client_->close();
}