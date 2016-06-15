//
//  redis_parser.c
//  supex
//  Modified by laochen 增加了redis命令适配（改遍自redis里面的源码)
//  Created by 周凯 on 16/1/16.
//  Copyright © 2016年 zhoukai. All rights reserved.
//

#include "redis_parser.h"


/*普通消息中可以包含的字符*/
static const char PRINT_CH[256] = {
    /*   0 nul    1 soh    2 stx    3 etx    4 eot    5 enq    6 ack    7 bel  */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*   8 bs     9 ht    10 nl    11 vt    12 np    13 cr    14 so    15 si   */
    0,      '\t',     0,     0,       0,       0,       0,       0,
    /*  16 dle   17 dc1   18 dc2   19 dc3   20 dc4   21 nak   22 syn   23 etb */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*  24 can   25 em    26 sub   27 esc   28 fs    29 gs    30 rs    31 us  */
    0,       0,       0,       0,       0,       0,       0,       0,
    /*  32 sp    33  !    34  "    35  #    36  $    37  %    38  &    39  '  */
    ' ',      '!',     '"',     '#',     '$',     '%',     '&',    '\'',
    /*  40  (    41  )    42  *    43  +    44  ,    45  -    46  .    47  /  */
    '(',    ')',     '*',     '+',     ',',     '-',     '.',     '/',
    /*  48  0    49  1    50  2    51  3    52  4    53  5    54  6    55  7  */
    '0',     '1',     '2',    '3',     '4',     '5',     '6',     '7',
    /*  56  8    57  9    58  :    59  ;    60  <    61  =    62  >    63  ?  */
    '8',     '9',     ':',    ';',      '<',     '=',     '>',     '?',
    /*  64  @    65  A    66  B    67  C    68  D    69  E    70  F    71  G  */
    0,      'a',     'b',     'c',     'd',     'e',     'f',     'g',
    /*  72  H    73  I    74  J    75  K    76  L    77  M    78  N    79  O  */
    'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
    /*  80  P    81  Q    82  R    83  S    84  T    85  U    86  V    87  W  */
    'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
    /*  88  X    89  Y    90  Z    91  [    92  \    93  ]    94  ^    95  _  */
    'x',     'y',     'z',      '[',       '\\',       ']',      '^',     '_',
    /*  96  `    97  a    98  b    99  c   100  d   101  e   102  f   103  g  */
    '`',     'a',     'b',     'c',     'd',     'e',     'f',     'g',
    /* 104  h   105  i   106  j   107  k   108  l   109  m   110  n   111  o  */
    'h',     'i',     'j',     'k',     'l',     'm',     'n',     'o',
    /* 112  p   113  q   114  r   115  s   116  t   117  u   118  v   119  w  */
    'p',     'q',     'r',     's',     't',     'u',     'v',     'w',
    /* 120  x   121  y   122  z   123  {   124  |   125  }   126  ~   127 del */
    'x',     'y',     'z',      '{',      '|',      '}',      '~',       0
};
/* gcc version. for example : v4.1.2 is 40102, v3.4.6 is 30406 */
#define GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)

/*
 *逻辑跳转优化
 */
#if GCC_VERSION
/*条件大多数为真，与if配合使用，直接执行if中语句*/
#define likely(x)     __builtin_expect(!!(x), 1)
/*条件大多数为假，与if配合使用，直接执行else中语句*/
#define unlikely(x)   __builtin_expect(!!(x), 0)
#else
#define likely(x)     (!!(x))
#define unlikely(x)   (!!(x))
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (static_cast<int>(sizeof(a) / sizeof((a)[0])))
#endif

redisCommand redisCommandTable[] = {
    {0x85f, "del", -2, "w", 0, 1},
    {0x1053, "get", 2, "rF", 0, 1},
    {0x3003, "set", -3, "wm", 0, 1},
    {0x3425, "ttl", 2, "rF", 0, 1},
    {0x36c5, "auth", 2, "rsltF", 0, 0},
    {0xd8cd, "decr", 2, "wmF", 0, 1},
    {0x118ac, "echo", 2, "rF", 0, 0},
    {0x14fc6, "exec", 1, "sM", 0, 0},
    {0x1e8f7, "hdel", -3, "wF", 0, 1},
    {0x1f0eb, "hget", 3, "rF", 0, 1},
    {0x1fe19, "hlen", 2, "rF", 0, 1},
    {0x2109b, "hset", 4, "wmF", 0, 1},
    {0x247d9, "incr", 2, "wmF", 0, 1},
    {0x2bba2, "keys", 2, "rS", 0, 0},
    {0x310b9, "llen", 2, "rF", 0, 1},
    {0x31c4f, "lpop", 2, "wF", 0, 1},
    {0x32090, "lrem", 4, "w", 0, 1},
    {0x3233b, "lset", 4, "wm", 0, 1},
    {0x34833, "mget", -2, "r", 0, 1},
    {0x367e3, "mset", -3, "wm", 0, 1},
    {0x41c50, "ping", -1, "rtF", 0, 0},
    {0x439fd, "pttl", 2, "rF", 0, 1},
    {0x4b83f, "rpop", 2, "wF", 0, 1},
    {0x4d421, "sadd", -3, "wmF", 0, 1},
    {0x4d925, "scan", -2, "rR", 0, 0},
    {0x4fa95, "sort", -2, "wm", 0, 1},
    {0x4fce7, "spop", 2, "wRsF", 0, 1},
    {0x50128, "srem", -3, "wF", 0, 1},
    {0x52ed4, "time", 1, "rRF", 0, 0},
    {0x55962, "type", 2, "rF", 0, 1},
    {0x6b4b9, "zadd", -4, "wmF", 0, 1},
    {0x6e1c0, "zrem", -3, "wF", 0, 1},
    {0x951f7, "bitop", -4, "wm", 0, 2},
    {0xa155f, "blpop", -3, "ws", 0, 1},
    {0xbb14f, "brpop", -3, "ws", 0, 1},
    {0x338b12, "hkeys", 2, "rS", 0, 1},
    {0x3417a3, "hmget", -3, "r", 0, 1},
    {0x343753, "hmset", -4, "wm", 0, 1},
    {0x35a895, "hscan", -3, "rR", 0, 1},
    {0x367268, "hvals", 2, "rS", 0, 1},
    {0x50f033, "lpush", -3, "wmF", 0, 1},
    {0x51f9e8, "ltrim", 4, "w", 0, 1},
    {0x5928e2, "multi", 1, "rsF", 0, 0},
    {0x6ca9ad, "append", 3, "wm", 0, 1},
    {0x7ac693, "rpush", -3, "wmF", 0, 1},
    {0x7e0e2d, "scard", 2, "rF", 0, 1},
    {0x7e66bf, "sdiff", -2, "rS", 0, 1},
    {0x7ec86b, "setex", 4, "wm", 0, 1},
    {0x7ec955, "setnx", 3, "wmF", 0, 1},
    {0x80e21e, "smove", 4, "wF", 0, 1},
    {0x825c45, "sscan", -3, "rR", 0, 1},
    {0xaedd9d, "zcard", 2, "rF", 0, 1},
    {0xb2e314, "zrank", 3, "rF", 0, 1},
    {0xb32bb5, "zscan", -3, "rR", 0, 1},
    {0xc48fe4, "setrange", 4, "wm", 0, 1},
    {0xf255b2, "bitpos", -3, "r", 0, 1},
    {0x23c7d86, "decrby", 3, "wmF", 0, 1},
    {0x2bcd4db, "zremrangebylex", 4, "w", 0, 1},
    {0x377e5b8, "exists", -2, "rF", 0, 1},
    {0x379aba6, "expire", 3, "wF", 0, 1},
    {0x460c5ff, "getbit", 3, "rF", 0, 1},
    {0x460f27b, "getset", 3, "wm", 0, 1},
    {0x573dab5, "hsetnx", 4, "wmF", 0, 1},
    {0x605b936, "incrby", 3, "wmF", 0, 1},
    {0x78058db, "zrangebylex", -4, "r", 0, 1},
    {0x8058d53, "lindex", 3, "r", 0, 1},
    {0x8386545, "lpushx", 3, "wmF", 0, 1},
    {0x840ece4, "lrange", 4, "r", 0, 1},
    {0x8fe54d5, "msetnx", -3, "wm", 0, 1},
    {0xb1e36cb, "psetex", 4, "wm", 0, 1},
    {0xc2967a4, "rename", 3, "w", 0, 1},
    {0xc782b05, "rpushx", 3, "wmF", 0, 1},
    {0xce0517f, "setbit", 4, "wm", 0, 1},
    {0xcfac8ed, "sinter", -2, "rS", 0, 1},
    {0xd487a19, "strlen", 2, "rF", 0, 1},
    {0xd4b3caf, "substr", 4, "r", 0, 1},
    {0xd4e59a1, "sunion", -2, "rS", 0, 1},
    {0x11c64c25, "zcount", 4, "rF", 0, 1},
    {0x122b0fa4, "zrange", -4, "r", 0, 1},
    {0x123295c6, "zscore", 3, "rF", 0, 1},
    {0x139e2153, "hincrbyfloat", 4, "wmF", 0, 1},
    {0x1781d927, "persist", 2, "wF", 0, 1},
    {0x17aae166, "pexpire", 3, "wF", 0, 1},
    {0x1d55ae79, "renamenx", 3, "wF", 0, 1},
    {0x2d49426b, "expireat", 3, "wF", 0, 1},
    {0x3ad24266, "zremrangebyscore", 4, "w", 0, 1},
    {0x3d6575ed, "discard", 1, "rsF", 0, 0},
    {0x3e0d18db, "zrevrangebylex", -4, "r", 0, 1},
    {0x472f2ea4, "zremrangebyrank", 4, "w", 0, 1},
    {0x642a8953, "incrbyfloat", 3, "wmF", 0, 1},
    {0x74a65265, "zlexcount", 4, "rF", 0, 1},
    {0x790cd13d, "sismember", 3, "rF", 0, 1},
    {0x7f3b316b, "pexpireat", 3, "wF", 0, 1},
    {0x7f8faae5, "bitcount", -2, "r", 0, 1},
    {0x81f4dec8, "randomkey", 1, "rR", 0, 0},
    {0x845ba978, "hexists", 3, "rF", 0, 1},
    {0x85448761, "hgetall", 2, "r", 0, 1},
    {0x86e97cf6, "hincrby", 4, "wmF", 0, 1},
    {0x894fe444, "smembers", 2, "rS", 0, 1},
    {0x8ff831e4, "getrange", 4, "r", 0, 1},
    {0xa491f1ee, "sunionstore", -3, "wm", 0, 1},
    {0xa5aec56e, "sinterstore", -3, "wm", 0, 1},
    {0xa7dd47fd, "srandmember", -2, "rR", 0, 1},
    {0xa9c98c93, "rpoplpush", 3, "wm", 0, 1},
    {0xac06ada4, "zrevrange", -4, "r", 0, 1},
    {0xcf1ad266, "zrangebyscore", -4, "r", 0, 1},
    {0xd0945fbd, "linsert", 5, "wm", 0, 1},
    {0xd16ce693, "brpoplpush", 4, "wms", 0, 1},
    {0xd257bd76, "zincrby", 4, "wmF", 0, 1},
    {0xd2b3edee, "zunionstore", -4, "wm", 0, 0},
    {0xd3d0c16e, "zinterstore", -4, "wm", 0, 0},
    {0xdac5d266, "zrevrangebyscore", -4, "r", 0, 1},
    {0xfca7eeae, "sdiffstore", -3, "wm", 0, 1},
    {0xfcc52e14, "zrevrank", 3, "rF", 0, 1}
};

#define TABLE_SIZE (sizeof(redisCommandTable)/sizeof(redisCommandTable[0]))

/* ------ 枚举和数组映射 ------- */

#define REDIS_ERRNO_MAP(XX)       \
    /*00*/XX(OK, "+OK\r\n"),          \
    /*01*/XX(PROTO, "-PROTOCAL ERROR\r\n"), \
    /*02*/XX(UNKNOW, "-UNKNOW ERROR\r\n"),  \
    /*03*/XX(COMPLETED, "-HAS COMPLETED\r\n"), \
    /*04*/XX(INVALID_LENGTH, "-INVALID CHARACTER IN LENGTH-FIELD\r\n"), \
    /*05*/XX(INVALID_CONTENT, "-INVALID CHARACTER IN DIGITAL OR TEXT REPLY\r\n"), \
    /*06*/XX(INVALID_COMMAND_TOKEN, "-INVALID CHARACTER IN COMMAND\r\n"), \
    /*07*/XX(UNMATCH_COMMAND, "-UNKNOW THIS COMMAND\r\n"), \
    /*08*/XX(UNMATCH_COMMAND_KVS, "-THE NUMBER OF KEY-VALUE DOES NOT MATCH THIS COMMAND\r\n"), \
    /*09*/XX(CR_EXPECTED, "-CR CHARACTER EXPECTED\r\n"), \
    /*10*/XX(LF_EXPECTED, "-LF CHARACTER EXPECTED\r\n"), \
    /*11*/XX(CB_message_begin, "-THE ON_MESSAGE_BEGIN CALLBACK FAILED\r\n"),      \
    /*12*/XX(CB_content_len, "-THE ON_CONTENT_LEN CALLBACK FAILED\r\n"),          \
    /*13*/XX(CB_content, "-THE ON_CONTENT CALLBACK FAILED\r\n"),          \
    /*14*/XX(CB_message_complete, "-THE ON_MESSAGE_COMPLETE CALLBACK FAILED\r\n"),    \

enum redis_errno
{
#define REDIS_ERRNO_GEN(n, s) RDE_##n
    REDIS_ERRNO_MAP(REDIS_ERRNO_GEN)
#undef REDIS_ERRNO_GEN
};

struct
{
    const char      *name;
    const char      *desc;
} static redis_errno_tab[] = {
#define REDIS_ERRNO_GEN(n, s) { #n, s }
    REDIS_ERRNO_MAP(REDIS_ERRNO_GEN)
#undef REDIS_ERRNO_GEN
};

#define CR      '\r'
#define LF      '\n'
#define LOWER(c)        (unsigned char)((c) | 0x20)
#define UPPER(c)        (unsigned char)((c) & (~0x20))
#define IS_ALPHA(c)     (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define IS_NUM(c)       ((c) >= '0' && (c) <= '9')
#define IS_ALPHANUM(c)  (IS_ALPHA(c) || IS_NUM(c))
#define IS_TOKEN(x)     (PRINT_CH[(int)(x)])

#define MARK(FOR)                                   \
    do {                                            \
        if (!FOR##_mark) {                          \
            FOR##_mark = p;                         \
        }                                           \
    } while (0)

static inline uint64_t get_command_type(char* command);
static inline int binary_search(redisCommand redisCommandTable[], int start, int end, unsigned int key);


const char *redis_errno_name(int redis_errno)
{
    assert(redis_errno > -1 && redis_errno < ARRAY_SIZE(redis_errno_tab));
    return redis_errno_tab[redis_errno].name;
}

const char *redis_errno_description(int redis_errno)
{
    assert(redis_errno > -1 && redis_errno < ARRAY_SIZE(redis_errno_tab));
    return redis_errno_tab[redis_errno].desc;
}

// while循环
static inline int binary_search(redisCommand commandTable[], int start, int end, unsigned int key) {
    int mid;
    while (start <= end) {
        mid = start + (end - start) / 2; // 直接平均可能會溢位，所以用此算法
        if (commandTable[mid].order < key)
            start = mid + 1;
        else if (commandTable[mid].order > key)
            end = mid - 1;
        else
            return mid; // 最後檢測相等是因為多數搜尋狀況不是大於要不就小於
    }
    return -1;
}

static inline uint64_t get_command_type(char* command) {
    /*转换字母到数字，并累加*/
    char ch;
    uint64_t command_type = 0;
    int len = strlen(command);
    for (int pos = 0; pos < len; pos++) {
        ch = UPPER(command[pos]);
        if ((!IS_ALPHA(ch))) {
            return 0;
        }
        command_type *= 26;
        command_type += ch - 'A';
    }
    return command_type;
}


int check_command(std::vector<char*> fields)
{
    int arity;
    int size = fields.size();
    uint64_t command_type = get_command_type(fields[0]);

    int id = binary_search(redisCommandTable, 0, TABLE_SIZE - 1, command_type);
    if (id < 0) {
        // fprintf(stderr, "error command : %llu\n", parser->command_type);
        return -1;
    } else {
        arity = redisCommandTable[id].arity;
        if (arity < 0) {
            arity = - arity;
            if (size  < arity) {
                return -2;
            }
        } else {
            if (size != arity) {
                return -2;
            }
        }
    }
    return id;
}
