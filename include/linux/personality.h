/* Flags for bug emulation. These occupy the top three bytes. */
#define STICKY_TIMEOUTS		0x8000000
#define WHOLE_SECONDS		0x4000000

/* Personality types. These go in the low byte. */
#define PER_MASK		(0x00ff)
#define PER_LINUX		(0x0000)
#define PER_SVR4		(0x0001 | STICKY_TIMEOUTS)
#define PER_SVR3		(0x0002 | STICKY_TIMEOUTS)
#define PER_SCOSVR3		(0x0003 | STICKY_TIMEOUTS | WHOLE_SECONDS)
#define PER_WYSEV386		(0x0004 | STICKY_TIMEOUTS)
#define PER_ISCR4		(0x0005 | STICKY_TIMEOUTS)
