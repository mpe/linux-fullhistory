#ifndef _MIPS_BYTEORDER_H
#define _MIPS_BYTEORDER_H

#include <asm/types.h>

#if defined (__MIPSEB__)
#  include <linux/byteorder/big_endian.h>
#elif defined (__MIPSEL__)
#  include <linux/byteorder/little_endian.h>
#else
#  error "MIPS, but neither __MIPSEB__, nor __MIPSEL__???"
#endif

#endif /* _MIPS_BYTEORDER_H */
