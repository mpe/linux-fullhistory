#ifndef _UTIME_H
#define _UTIME_H

#include <sys/types.h>	/* I know - shouldn't do this, but .. */

struct utimbuf {
	time_t actime;
	time_t modtime;
};

#ifdef __cplusplus
extern "C" {
#endif

extern int utime(const char *filename, struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif
