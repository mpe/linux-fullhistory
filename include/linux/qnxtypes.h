/*
 *  Name                         : qnxtypes.h
 *  Author                       : Richard Frowijn
 *  Function                     : standard qnx types
 *  Version                      : 1.0
 *  Last modified                : 22-03-1998
 * 
 *  History                      : 22-03-1998 created
 * 
 */

#ifndef _QNX4TYPES_H
#define _QNX4TYPES_H

typedef unsigned short qnx4_nxtnt_t;
typedef unsigned char qnx4_ftype_t;

typedef struct {
	long xtnt_blk;
	long xtnt_size;
} qnx4_xtnt_t;

typedef unsigned short qnx4_muid_t;
typedef unsigned short qnx4_mgid_t;
typedef unsigned long qnx4_off_t;
typedef unsigned short qnx4_nlink_t;

#endif
