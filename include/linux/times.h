#ifndef _LINUX_TIMES_H
#define _LINUX_TIMES_H

struct tms {
	time_t tms_utime;
	time_t tms_stime;
	time_t tms_cutime;
	time_t tms_cstime;
};

#endif
