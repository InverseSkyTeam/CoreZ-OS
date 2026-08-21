// 参考: mongoose/src/config.h, mongoose/src/arch.h
#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long time_t;

#define MG_ARCH MG_ARCH_CUSTOM  

#define MG_ENABLE_TCPIP 1         
#define MG_ENABLE_IPV6 0
#define MG_ENABLE_LOG 0          
#define MG_ENABLE_SOCKET 0
#define MG_ENABLE_CUSTOM_MILLIS 1  
#define MG_ENABLE_CUSTOM_RANDOM 1  
#define MG_ENABLE_CUSTOM_CALLOC 1  
#define MG_ENABLE_TCPIP_DRIVER_INIT 0  
#define MG_ENABLE_POSIX_FS 0
#define MG_ENABLE_MD5 0