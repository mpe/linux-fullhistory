#ifndef _M68K_MVME16xHW_H_
#define _M68K_MVME16xHW_H_

#include <asm/irq.h>

/* Board ID data structure - pointer to this retrieved from Bug by head.S */

/* Note, bytes 12 and 13 are board no in BCD (0162,0166,0167,0177,etc) */

extern long mvme_bdid_ptr;

typedef struct {
	char	bdid[4];
	u_char	rev, mth, day, yr;
	u_short	size, reserved;
	u_short	brdno;
	char brdsuffix[2];
	u_long	options;
	u_short	clun, dlun, ctype, dnum;
	u_long	option2;
} t_bdid, *p_bdid;


typedef struct {
	u_char	ack_icr,
		flt_icr,
		sel_icr,
		pe_icr,
		bsy_icr,
		spare1,
		isr,
		cr,
		spare2,
		spare3,
		spare4,
		data;
} lpr_ctrl;

#define LPR_REGS	((volatile lpr_ctrl *)0xfff42030)

#define I596_BASE	0xfff46000

#define SCC_A_ADDR	0xfff45005
#define SCC_B_ADDR	0xfff45001

#define IRQ_MVME162_TYPE_PRIO	0

#define IRQ_MVME167_PRN		0x54
#define IRQ_MVME16x_I596	0x57
#define IRQ_MVME16x_SCSI	0x55
#define IRQ_MVME16x_FLY		0x7f
#define IRQ_MVME167_SER_ERR	0x5c
#define IRQ_MVME167_SER_MODEM	0x5d
#define IRQ_MVME167_SER_TX	0x5e
#define IRQ_MVME167_SER_RX	0x5f
#define IRQ_MVME16x_TIMER	0x59

/* SCC interrupts, for MVME162 */
#define IRQ_MVME162_SCC_BASE		0x40
#define IRQ_MVME162_SCCB_TX		0x40
#define IRQ_MVME162_SCCB_STAT		0x42
#define IRQ_MVME162_SCCB_RX		0x44
#define IRQ_MVME162_SCCB_SPCOND		0x46
#define IRQ_MVME162_SCCA_TX		0x48
#define IRQ_MVME162_SCCA_STAT		0x4a
#define IRQ_MVME162_SCCA_RX		0x4c
#define IRQ_MVME162_SCCA_SPCOND		0x4e

/* MVME162 version register */

#define MVME162_VERSION_REG	0xfff4202e

extern unsigned short mvme16x_config;

/* Lower 8 bits must match the revision register in the MC2 chip */

#define MVME16x_CONFIG_SPEED_32		0x0001
#define MVME16x_CONFIG_NO_VMECHIP2	0x0002
#define MVME16x_CONFIG_NO_SCSICHIP	0x0004
#define MVME16x_CONFIG_NO_ETHERNET	0x0008
#define MVME16x_CONFIG_GOT_FPU		0x0010

#define MVME16x_CONFIG_GOT_LP		0x0100
#define MVME16x_CONFIG_GOT_CD2401	0x0200
#define MVME16x_CONFIG_GOT_SCCA		0x0400
#define MVME16x_CONFIG_GOT_SCCB		0x0800

/* Specials for the ethernet driver */

#define CA()		(((struct i596_reg *)dev->base_addr)->ca = 1)

#define MPU_PORT(c,x)	\
  ((struct i596_reg *)(dev->base_addr))->porthi = ((c) | (u32)(x)) & 0xffff; \
  ((struct i596_reg *)(dev->base_addr))->portlo = ((c) | (u32)(x)) >> 16

#define SCP_SYSBUS	0x00000054

#define WSWAPrfd(x)	((struct i596_rfd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPrbd(x)	((struct i596_rbd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPiscp(x)	((struct i596_iscp *)(((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPscb(x)	((struct i596_scb *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPcmd(x)	((struct i596_cmd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPtbd(x)	((struct i596_tbd *) (((u32)(x)<<16) | ((((u32)(x)))>>16)))
#define WSWAPchar(x)	((char *)            (((u32)(x)<<16) | ((((u32)(x)))>>16)))

/*
 * The MPU_PORT command allows direct access to the 82596. With PORT access
 * the following commands are available (p5-18). The 32-bit port command
 * must be word-swapped with the most significant word written first.
 */
#define PORT_RESET	0x00	/* reset 82596 */
#define PORT_SELFTEST	0x01	/* selftest */
#define PORT_ALTSCP	0x02	/* alternate SCB address */
#define PORT_ALTDUMP	0x03	/* Alternate DUMP address */

#define ISCP_BUSY	0x00010000

#endif /* _M68K_MVME16xHW_H_ */
