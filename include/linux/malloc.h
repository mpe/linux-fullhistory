#ifndef _LINUX_MALLOC_H
#define _LINUX_MALLOC_H

#include <linux/mm.h>

void * kmalloc(unsigned int size, int priority);
void kfree(void * obj);

#define kfree_s(a,b) kfree(a)

#endif /* _LINUX_MALLOC_H */
