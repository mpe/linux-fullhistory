/*
 *	Apple Peripheral System Controller (PSC)
 *
 *	The PSC is used on the AV Macs to control IO functions not handled
 *	by the VIAs (Ethernet, DSP, SCC).
 */
 
#define PSCBASE		0x50F31000

/*
 *	The IER/IFR registers work like the VIA, except that it has 4
 *	of them each on different interrupt levels.
 */
 
#define pIFR3		0x130
#define pIFR4		0x140
#define pIFR5		0x150
#define pIFR6		0x160

#define pIER3		0x134
#define pIER4		0x144
#define pIER5		0x154
#define pIER6		0x164

/*
 *	Ethernet Control Registers
 */
 
#define PSC_ENETRD_CTL  0xc10
#define PSC_ENETWR_CTL  0xc20

/*
 *	Receive DMA channel (add +0x10 for 2nd channel)
 */
 
#define PSC_ENETRD_ADDR 0x1020 
#define PSC_ENETRD_LEN  0x1024
#define PSC_ENETRD_CMD  0x1028

/*
 *	Transmit DMA channel (add +0x10 for 2nd channel)
 */
 
#define PSC_ENETWR_ADDR 0x1040
#define PSC_ENETWR_LEN  0x1044
#define PSC_ENETWR_CMD  0x1048

/*
 *	Access functions
 */
 
extern volatile unsigned char *psc;

extern inline void psc_write_word(int offset, u16 data)
{
	*((volatile u16 *)(psc+offset)) = data;
}

extern inline void psc_write_long(int offset, u32 data)
{
	*((volatile u32 *)(psc+offset)) = data;
}

extern inline u16 psc_read_word(int offset)
{
	return *((volatile u16 *)(psc+offset));
}

extern inline u32 psc_read_long(int offset)
{
	return *((volatile u32 *)(psc+offset));
}

