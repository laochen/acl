#!/bin/sh

valgrind --tool=memcheck --leak-check=yes -v ./redis_aio_proxy alone
