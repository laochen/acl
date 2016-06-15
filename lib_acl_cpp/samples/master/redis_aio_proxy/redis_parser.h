//
//  redis_parser.h
//  supex
//
//  Created by 周凯 on 16/1/16.
//  Copyright © 2016年 zhoukai. All rights reserved.
//

#ifndef LIB_ACL_CPP_SAMPLES_REDIS_AIO_PROXY_REDIS_PARSER_H_
#define LIB_ACL_CPP_SAMPLES_REDIS_AIO_PROXY_REDIS_PARSER_H_

#include "stdafx.h"
#if defined(__cplusplus)
#if !defined(__BEGIN_DECLS)
#define __BEGIN_DECLS \
    extern "C" {
#define __END_DECLS \
    }
#endif
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

__BEGIN_DECLS


typedef struct redisCommand redisCommand;
struct redisCommand {
  unsigned int order;
  const char *name;
  int arity;
  const char *sflags; /* Flags as string representation, one char per flag. */
  int flags;    /* The actual flags, obtained from the 'sflags' field. */
  /* What keys should be loaded in background when calling this command? */
  int firstkey; /* The first argument that's a key (0 = no keys) */
};

extern redisCommand redisCommandTable[];

/**
 * 根据错误值获取错误名称
 */
const char *redis_errno_name(int redis_errno);

/**
 * 根据错误值获取错误描述，符合redis错误响应字串
 */
const char *redis_errno_description(int redis_errno);

int check_command(std::vector<char*> fields);

__END_DECLS
#endif  /*  LIB_ACL_CPP_SAMPLES_REDIS_AIO_PROXY_REDIS_PARSER_H_ */
