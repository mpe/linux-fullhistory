/*********************************************************************
 *                
 * Filename:      irvtd_driver.h
 * Version:       0.1
 * 
 *     Copyright (c) 1998, Takahide Higuchi <thiguchi@pluto.dti.ne.jp>, 
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     I, Takahide Higuchi, provide no warranty for any of this software. 
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/tty.h>

#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/irttp.h>


#define VSP_TYPE_NORMAL   1
#define VSP_TYPE_CALLOUT  2
#define IRCOMM_MAJOR  60;  /* Zero means automatic allocation
                              60,61,62,and 63 is reserved for experiment */
#define IRVTD_MINOR 64
#define IRVTD_CALLOUT_MINOR 32

#define IRVTD_TYPE_NORMAL 11
#define IRVTD_TYPE_CALLOUT 12


#define IRCOMM_HEADER 1       
#define IRCOMM_RESERVE LAP_HEADER+LMP_HEADER+TTP_HEADER+IRCOMM_HEADER





/*
 * Definitions for ircomm_cb_struct flags field
 *  this section is "stolen" from linux-kernel (drivers/char/serial.c)
 */
#define IRVTD_ASYNC_HUP_NOTIFY 0x0001 /* Notify getty on hangups and closes 
				   on the callout port */
/* #define IRVTD_ASYNC_FOURPORT  0x0002 */
/* Set OU1, OUT2 per AST Fourport settings */

#define IRVTD_ASYNC_SAK	0x0004	/* Secure Attention Key (Orange book) */

#define IRVTD_ASYNC_SPLIT_TERMIOS 0x0008 /* Separate termios for dialin/callout */

#define IRVTD_ASYNC_SPD_MASK	0x0030
#define IRVTD_ASYNC_SPD_HI	0x0010	/* Use 56000 instead of 38400 bps */

#define IRVTD_ASYNC_SPD_VHI	0x0020  /* Use 115200 instead of 38400 bps */
#define IRVTD_ASYNC_SPD_CUST	0x0030  /* Use user-specified divisor */

#define IRVTD_ASYNC_SKIP_TEST	0x0040  /* Skip UART test during autoconfiguration */
#define IRVTD_ASYNC_AUTO_IRQ    0x0080  /* Do automatic IRQ during autoconfiguration */
#define IRVTD_ASYNC_SESSION_LOCKOUT 0x0100 /* Lock out cua opens based on session */
#define IRVTD_ASYNC_PGRP_LOCKOUT    0x0200  /* Lock out cua opens based on pgrp */
#define IRVTD_ASYNC_CALLOUT_NOHUP   0x0400 /* Don't do hangups for cua device */

#define IRVTD_ASYNC_FLAGS	0x0FFF	/* Possible legal async flags */
#define IRVTD_ASYNC_USR_MASK 0x0430	/* Legal flags that non-privileged
				 * users can set or reset */

/* Internal flags used only by kernel/chr_drv/serial.c */
#define IRVTD_ASYNC_INITIALIZED	0x80000000 /* Serial port was initialized */
#define IRVTD_ASYNC_CALLOUT_ACTIVE	0x40000000 /* Call out device is active */
#define IRVTD_ASYNC_NORMAL_ACTIVE	0x20000000 /* Normal device is active */
#define IRVTD_ASYNC_BOOT_AUTOCONF	0x10000000 /* Autoconfigure port on bootup */
#define IRVTD_ASYNC_CLOSING		0x08000000 /* Serial port is closing */
#define IRVTD_ASYNC_CTS_FLOW		0x04000000 /* Do CTS flow control */
#define IRVTD_ASYNC_CHECK_CD		0x02000000 /* i.e., CLOCAL */
#define IRVTD_ASYNC_SHARE_IRQ		0x01000000 /* for multifunction cards */


#define IRVTD_ASYNC_CLOSING_WAIT_INF  0
#define IRVTD_ASYNC_CLOSING_WAIT_NONE 65535

/**************************************/

#define DELTA_DTR 0x01
#define DELTA_RTS 0x02
#define MCR_DTR 0x04
#define MCR_RTS 0x08

#define DELTA_CTS 0x01
#define DELTA_DSR 0x02
#define DELTA_RI  0x04 
#define DELTA_DCD 0x08
#define MSR_CTS   0x10
#define MSR_DSR   0x20
#define MSR_RI    0x40 
#define MSR_DCD   0x80

#define LSR_OE     0x02    /* Overrun error indicator */
#define LSR_PE     0x04    /* Parity error indicator */
#define LSR_FE     0x08    /* Frame error indicator */
#define LSR_BI     0x01    /* Break interrupt indicator */



/**************************************/




int irvtd_register_ttydriver(void);
void irvtd_unregister_ttydriver(void);

void irvtd_flush_chars(struct tty_struct *tty);




