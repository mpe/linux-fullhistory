#ifndef _XOR_H
#define _XOR_H

#include <linux/raid/md.h>

#define MAX_XOR_BLOCKS 4

extern void calibrate_xor_block(void);
extern void (*xor_block)(unsigned int count,
                         struct buffer_head **bh_ptr);

#endif
