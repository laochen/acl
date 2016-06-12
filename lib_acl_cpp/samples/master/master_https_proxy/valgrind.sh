#!/bin/sh

valgrind --tool=memcheck --leak-check=yes -v ./master_https_proxy alone
