/*
 *	Apple Peripheral System Controller (PSC)
 *
 *	The PSC is used on the AV Macs to control IO functions not handled
 *	by the VIAs (Ethernet, DSP, SCC).
 */
 
#define PSCBASE		0x50F31000

#define pIFR3		0x130
#define pIFR4		0x140
#define pIFR5		0x150
#define pIFR6		0x160

#define pIER3		0x134
#define pIER4		0x144
#define pIER5		0x154
#define pIER6		0x164
