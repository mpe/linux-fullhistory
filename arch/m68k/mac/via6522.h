/*
 *	6522 Versatile Interface Adapter (VIA)
 *
 *	There are two of these on the Mac II. Some IRQ's are vectored
 *	via them as are assorted bits and bobs - eg rtc, adb. The picture
 *	is a bit incomplete as the Mac documentation doesnt cover this well
 */
 
#ifndef _ASM_VIA6522_H_
#define _ASM_VIA6522_H_

#define VIABASE		0x50F00000
#define VIABASE2	0x50F02000

/*
 *	Not all of these are true post MacII I think
 */
 
#define VIA1A_vSccWrReq	0x80	/* SCC write */
#define VIA1A_vRev8	0x40	/* Revision 8 board ??? */
#define VIA1A_vHeadSel	0x20	/* Head select for IWM */
#define VIA1A_vOverlay	0x10
#define VIA1A_vSync	0x08
#define VIA1A_vVolume	0x07	/* Audio volume mask */
	
#define VIA1B_vSound	0x80	/* Audio on/off */
#define VIA1B_vMystery	0x40
#define VIA1B_vADBS2	0x20	/* ADB state 2 */
#define VIA1B_vADBS1	0x10	/* ADB state 1 */
#define VIA1B_vADBInt	0x08	/* ADB interrupt */
#define VIA1B_vRTCEnb	0x04	/* Real time clock */
#define VIA1B_vRTCClk	0x02
#define VIA1B_vRTCData	0x01

/*
 *	VIA2 A register is the interrupt lines raised off the nubus 
 *	slots.
 */
 
#define VIA2A_vIRQE	0x20
#define VIA2A_vIRQD	0x10
#define VIA2A_vIRQC	0x08
#define VIA2A_vIRQB	0x04
#define VIA2A_vIRQA	0x02
#define VIA2A_vIRQ9	0x01

/*
 *	Register B has the fun stuff in it
 */

#define VIA2B_vMode32	0x08	/* 24/32bit switch - doubles as cache flush */
#define VIA2B_vPower	0x04	/* Off switch */
#define VIA2B_vBusLk	0x02	/* Nubus in use ?? */
#define VIA2B_vCDis	0x01	/* Cache disable */

/*
 *	The 6522 via is a 2MHz part, and needs a delay. MacOS seems to
 *	execute MOV (Ax),(Ax) for this... Oh and we can't use udelay
 *	here... see we need the via to calibrate the udelay loop ...
 */

extern volatile long *via_memory_bogon;
 
extern __inline__ void via_write(volatile unsigned char *via,int reg, int v)
{
	*via_memory_bogon;
	*via_memory_bogon;
	*via_memory_bogon;
	via[reg]=v;
}

extern __inline__ int via_read(volatile unsigned char *via,int reg)
{
	*via_memory_bogon;
	*via_memory_bogon;
	*via_memory_bogon;
	return (int)via[reg];
}

extern volatile unsigned char *via1,*via2;

/*
 *	6522 registers - see databook
 */
 
#define vBufB	0x0000
#define vBufA	0x0200
#define vDirB	0x0400
#define vDirA	0x0600
#define vT1CL	0x0800
#define vT1CH	0x0a00
#define vT1LL	0x0c00
#define vT1LH	0x0e00
#define vT2CL	0x1000
#define vT2CH	0x1200
#define vSR	0x1400
#define vACR	0x1600
#define vPCR	0x1800
#define vIFR	0x1a00
#define vIER	0x1c00 
#define vANH	0x1e00  /* register A (no shake) */

#define rBufB	0x00
#define rBufA	0x02
/*#define rIFR	0x03*/
#define rIFR	0x1A03
#define rVideo	0x10
#define rSlot	0x12
/*#define rIER	0x13*/
#define rIER	0x1C13
/*
#define R_rIFR	0x03
#define R_rIER	0x13
#define W_rIFR	0x1A03
#define W_rIER	0x1C13
*/
/*
 *	VIA interrupt
 */
 
struct via_irq_tab
{
	void (*vector[8])(int, void *, struct pt_regs *);
};

extern void via1_irq(int, void *, struct pt_regs *);
extern void via2_irq(int, void *, struct pt_regs *);

extern void via_setup_keyboard(void);

#endif /* _ASM_VIA6522_H_ */
