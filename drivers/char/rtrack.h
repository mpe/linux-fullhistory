/* RadioTrack (RadioReveal) include file.
 * (c) 1997 M. Kirkwood
 *
 * Not in include/linux/ because there's no need for anyone
 * to know about these details, I reckon.
 */

#ifndef __RTRACK_H
#define __RTRACK_H

#include <linux/radio.h>

void radiotrack_init(void);
int rt_setvol(struct radio_device *dev, int vol);
int rt_setband(struct radio_device *dev, int vol);
int rt_setfreq(struct radio_device *dev, int vol);
int rt_getsigstr(struct radio_device *dev);

/* frequency encoding stuff... */
/* we have to careful not to introduce fp stuff here */
#define	RTRACK_ENCODE(x)	(((((x)*2)/5)-(40*88))+0xf6c)
#define	RTRACK_DECODE(x)	(((((x)-0xf6c)+(40*88))*5)/2)
/* we shouldn't actually need the decode macro (or the excessive bracketing :-) */

#endif /* __RTRACK_H */
