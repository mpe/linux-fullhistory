/*
 *  linux/lib/string.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/string.h>

/* all the actual functions should be inline anyway, so.. */

char * ___strtok = NULL;
