#ifndef _BOUNCE_H_
#define _BOUNCE_H_

#include <linux/types.h>

/* Each page that is used for a bounce buffer has the following header
 * at the top which points to the next page in the free or in-use list
 * and provides some other stats for drivers.
 */

#define NR_BOUNCE_PAGES 32

struct bounce_page
{
  struct bounce_page *next;
  __u32 size;
  __u32 remaining;
  __u8 *current;
  __u8 data[PAGE_SIZE - 1*sizeof(struct bounce_page *)
	   - 2*sizeof(__u32) - sizeof(__u8)];
};

#endif
