#ifndef SUN3X_TIME_H
#define SUN3X_TIME_H

extern void sun3x_gettod (int *yearp, int *monp, int *dayp,
                   int *hourp, int *minp, int *secp);
extern int sun3x_hwclk(int set, struct hwclk_time *t);
unsigned long sun3x_gettimeoffset (void);
void sun3x_sched_init(void (*vector)(int, void *, struct pt_regs *));

struct mostek_dt {
	volatile unsigned char csr;
	volatile unsigned char sec;
	volatile unsigned char min;
	volatile unsigned char hour;
	volatile unsigned char wday;
	volatile unsigned char mday;
	volatile unsigned char month;
	volatile unsigned char year;
};

#endif
