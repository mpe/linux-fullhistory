/*
 * Berkshire PC Watchdog Defines
 * For version 0.41 of the driver
 */

#define	PCWD_IOCTL_BASE	'W'

#define	PCWD_GETSTAT	_IOR(PCWD_IOCTL_BASE, 1, int)
#define	PCWD_PING	_IOR(PCWD_IOCTL_BASE, 2, int)

#define	PCWD_PREVRESET	0x01	/* System previously reset by card */
#define	PCWD_TEMPSENSE	0x02	/* Temperature overheat sense */
