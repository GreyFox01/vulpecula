#pragma once
#include <stddef.h>
#include <string.h>
static inline int mbedtls_sha256(const unsigned char*i,size_t n,unsigned char o[32],int){memset(o,0,32);(void)i;(void)n;return 0;}
