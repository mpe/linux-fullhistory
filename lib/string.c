/*
 *  linux/lib/string.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef __GNUC__
#error I want gcc!
#endif

#include <linux/types.h>

#ifdef __cplusplus
#define C_BEGIN extern "C" {
#define C_END }
#else
#define C_BEGIN
#define C_END
#endif

#undef __cplusplus
#define extern
#define inline
#define __LIBRARY__

C_BEGIN
#include <linux/string.h>
C_END
