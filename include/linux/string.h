#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include <linux/types.h>	/* for size_t */

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef i386

#include <asm/string.h>		/* inline functions for i386.. */

#else

extern char * strcpy(char *, const char *);
extern char * strncpy(char *, const char *, size_t);
extern char * strcat(char *, const char *);
extern char * strncat(char *, const char *, size_t);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern char * strchr(const char *, char);
extern char * strrchr(const char *, char);
extern size_t strspn(const char *, const char *);
extern size_t strcspn(const char *, const char *);
extern char * strpbrk(const char *, const char *);
extern char * strstr(const char *, const char *);
extern size_t strlen(const char *);
extern char * strtok(char *, const char *);
extern void * memcpy(void *, const void *, size_t);
extern void * memmove(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern void * memchr(const void *, char, size_t);
extern void * memset(void *, char, size_t);

#endif

#ifdef __cplusplus
}
#endif

#endif
