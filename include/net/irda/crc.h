/*********************************************************************
 *                
 * Filename:      crc.h
 * Version:       
 * Description:   CRC routines
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Aug  4 20:40:53 1997
 * Modified at:   Tue Dec 15 22:18:53 1998
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 ********************************************************************/

#ifndef IR_CRC_H
#define IR_CRC_H

#include <linux/types.h>

#define INIT_FCS  0xffff   /* Initial FCS value */
#define GOOD_FCS  0xf0b8   /* Good final FCS value */

/* Recompute the FCS with one more character appended. */
#define IR_FCS(fcs, c) (((fcs) >> 8) ^ irda_crc16_table[((fcs) ^ (c)) & 0xff])

/* Recompute the FCS with len bytes appended. */
unsigned short crc_calc( __u16 fcs, __u8 const *buf, size_t len);

extern __u16 const irda_crc16_table[];

#endif
