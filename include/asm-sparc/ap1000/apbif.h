  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */

/*
**	BIF data registers (system mode)
*/
#define BIF_DATA 	(BIF+0x0000)	/* BIF send and receive data registe	*/
#define BIF_EDATA  	(BIF+0x0004)	/* BIF end data register 				*/
/*
**	BIF data registers (user mode) 
*/
#define UBIF_DATA 	(UBIF+0x0000)	/* BIF send and receive data registe	*/
#define UBIF_EDATA  (UBIF+0x0004)	/* BIF end data register 				*/

/*
**	BIF scatter and gather parameter register (system mode) 
*/
#define BIF_X0SK 	(BIF+0x0010)	/* initial X-skip register 			*/
#define BIF_XSK 	(BIF+0x0014)	/* X-skip register 					*/
#define BIF_XSZ 	(BIF+0x0018)	/* X-size register 					*/

#define BIF_Y0SK 	(BIF+0x001c)	/* initial Y-skip register 			*/
#define BIF_YSK 	(BIF+0x0020)	/* Y-skip register 					*/
#define BIF_YSZ 	(BIF+0x0024)	/* Y-size register 					*/

#define BIF_CX0SK 	(BIF+0x0028)	/* initial counter of X-skip 		*/
#define BIF_CXSK 	(BIF+0x002c)	/* X-skip counter 					*/
#define BIF_CXSZ 	(BIF+0x0030)	/* X-size counter 					*/

#define BIF_CY0SK 	(BIF+0x0034)	/* initial counter of Y-skip 		*/
#define BIF_CYSK 	(BIF+0x0038)	/* Y-skip counter 					*/
#define BIF_CYSZ 	(BIF+0x003c)	/* Y-size counter 					*/

#define BIF_TTL 	(BIF+0x0040)	/* number of data transfer register */
#define BIF_CTTL 	(BIF+0x0044)	/* number of data transfer counter	*/

/*
**	BIF scatter and gather parameter register (user mode) 
*/
#define UBIF_X0SK 	(UBIF+0x0010)	/* initial X-skip register 			*/
#define UBIF_XSK 	(UBIF+0x0014)	/* X-skip register 					*/
#define UBIF_XSZ 	(UBIF+0x0018)	/* X-size register 					*/

#define UBIF_Y0SK 	(UBIF+0x001c)	/* initial Y-skip register 			*/
#define UBIF_YSK 	(UBIF+0x0020)	/* Y-skip register 					*/
#define UBIF_YSZ 	(UBIF+0x0024)	/* Y-size register 					*/

#define UBIF_CX0SK 	(UBIF+0x0028)	/* initial counter of X-skip 		*/
#define UBIF_CXSK 	(UBIF+0x002c)	/* X-skip counter 					*/
#define UBIF_CXSZ 	(UBIF+0x0030)	/* X-size counter 					*/

#define UBIF_CY0SK 	(UBIF+0x0034)	/* initial counter of Y-skip 		*/
#define UBIF_CYSK 	(UBIF+0x0038)	/* Y-skip counter 					*/
#define UBIF_CYSZ 	(UBIF+0x003c)	/* Y-size counter 					*/

#define UBIF_TTL 	(UBIF+0x0040)	/* number of data transfer register */
#define UBIF_CTTL 	(UBIF+0x0044)	/* number of data transfer counter	*/

/*
**	BIF control registers (system mode)
*/
#define BIF_CIDR0 	(BIF+0x0048)	/* cell-id register 0 					*/
#define BIF_CIDR1 	(BIF+0x004c)	/* cell-id register 1 (for cell mode) 	*/
#define BIF_CIDR2 	(BIF+0x0050)	/* cell-id register 2 					*/
#define BIF_CIDR3 	(BIF+0x0054)	/* cell-id register 3 					*/
#define BIF_HEADER 	(BIF+0x0058)	/* header register 						*/
#define BIF_INTR 	(BIF+0x006c)	/* BIF interrupt control register 		*/
#define BIF_SDCSR 	(BIF+0x0070)	/* BIF data control set register 		*/
#define BIF_RDCSR 	(BIF+0x0074)	/* BIF data control reset reregister	*/
#define BIF_MHOCR 	(BIF+0x0078)	/* BIF extentional control reregister 	*/

/*
**	BIF control registers (user mode)
*/
#define UBIF_CIDR0 	(UBIF+0x0048)	/* cell-id register 0 					*/
#define UBIF_CIDR1 	(UBIF+0x004c)	/* cell-id register 1 (for cell mode) 	*/
#define UBIF_CIDR2 	(UBIF+0x0050)	/* cell-id register 2 					*/
#define UBIF_CIDR3 	(UBIF+0x0054)	/* cell-id register 3 					*/
#define UBIF_HEADER (UBIF+0x0058)	/* header register 						*/
#define UBIF_INTR 	(UBIF+0x006c)	/* BIF interrupt control register 		*/
#define UBIF_SDCSR 	(UBIF+0x0070)	/* BIF data control set register 		*/
#define UBIF_RDCSR 	(UBIF+0x0074)	/* BIF data control reset reregister	*/
#define UBIF_MHOCR 	(UBIF+0x0078)	/* BIF extentional control reregister 	*/

/*
** bit assignment 
*/
#define BIF_HEADER_ID	 0xffff0000	/* cell-id   		*/
#define BIF_HEADER_BR	 0x00008000	/* broad bit 		*/
#define BIF_HEADER_IS	 0x00006000	/* ID select		*/
#define BIF_HEADER_IS_00 0x00000000 
#define BIF_HEADER_IS_01 0x00002000 
#define BIF_HEADER_IS_10 0x00004000 
#define BIF_HEADER_IS_11 0x00006000 
#define BIF_HEADER_IN	 0x00001000	/* interrupt bit	*/
#define BIF_HEADER_LS	 0x00000800	/* line send		*/
#define BIF_HEADER_SC	 0x00000400	/* scatter bit 		*/
#define BIF_HEADER_HS	 0x00000200	/* header strip		*/
#define BIF_HEADER_RS	 0x00000100	/* bus release 		*/

#define BIF_HEADER_ID_SHIFT 16

#define BIF_INTR_GS		0x00020000	/* grant interrupt select 		*/
#define BIF_INTR_GM		0x00010000	/* grant interrupt mask			*/
#define BIF_INTR_GI		0x00008000	/* grant interrupt request		*/
#define BIF_INTR_HS		0x00004000	/* header interrupt select		*/
#define BIF_INTR_HM		0x00002000	/* header interrupt mask		*/
#define BIF_INTR_HI		0x00001000	/* header interrupt request		*/
#define BIF_INTR_SS		0x00000800	/* send interrupt select		*/
#define BIF_INTR_SM		0x00000400	/* send interrupt mask			*/
#define BIF_INTR_SI		0x00000200	/* send interrupt request		*/
#define BIF_INTR_RS		0x00000100	/* receive interrupt select		*/
#define BIF_INTR_RM		0x00000080	/* receive interrupt mask		*/
#define BIF_INTR_RI		0x00000040	/* receive interrupt request	*/
#define BIF_INTR_ES		0x00000020	/* error interrupt select 		*/
#define BIF_INTR_EM		0x00000010	/* error interrupt mask 		*/
#define BIF_INTR_EI		0x00000008	/* error interrupt request 		*/
#define BIF_INTR_AS		0x00000004	/* attention interrupt select 	*/
#define BIF_INTR_AM		0x00000002	/* attention interrupt mask 	*/
#define BIF_INTR_AI		0x00000001	/* attention interrupt request  */

#define BIF_SDCSR_ER    0x7fffc000  /* error  detected by BIF */
#define BIF_SDCSR_PE	0x80000000	/* detect parity error in sync			*/
#define BIF_SDCSR_SP	0x40000000	/* parity error in sync					*/
#define BIF_SDCSR_LP	0x20000000	/* L-bus parity error					*/
#define BIF_SDCSR_LR	0x10000000	/* */
#define BIF_SDCSR_LW	0x08000000	/* */
#define BIF_SDCSR_AL	0x04000000	/* specify end bit except of end data	*/
#define BIF_SDCSR_SS	0x02000000	/* request bit but masked by slow sync	*/
#define BIF_SDCSR_SC	0x01000000	/* clear bit but masked by slow sync	*/
#define BIF_SDCSR_SY	0x00800000	/* set bit but masked by slow status	*/
#define BIF_SDCSR_FS	0x00400000	/* request bit but masked by fast sync	*/
#define BIF_SDCSR_FC	0x00200000	/* clear bit but masked by fast sync	*/
#define BIF_SDCSR_FY	0x00100000	/* set bit but masked by fast status	*/
#define BIF_SDCSR_CP	0x00080000	/* parity error in commnad bus			*/
#define BIF_SDCSR_FP	0x00040000	/* execute scatter or gather but FN=0	*/
#define BIF_SDCSR_PS	0x00020000	/* header receive error 				*/
#define BIF_SDCSR_RA	0x00010000	/* change scatter,gather parameter		*/
#define BIF_SDCSR_PA	0x00008000	/* check if send or receive error		*/
#define BIF_SDCSR_DL	0x00004000	/* lost data							*/
#define BIF_SDCSR_BB	0x00002000	/* check if some BIF use command bus	*/
#define BIF_SDCSR_BG	0x00001000	/* check if command bus got				*/
#define BIF_SDCSR_BR	0x00000800	/* request command bus					*/
#define BIF_SDCSR_CN	0x00000400	/* release BIF from command bus			*/
#define BIF_SDCSR_FN	0x00000200	/* scatter gather enable				*/
#define BIF_SDCSR_EB	0x00000100	/* send data that have end bit			*/
#define BIF_SDCSR_TB	0x000000E0	/* data in send FIFO					*/
#define BIF_SDCSR_TB4	0x00000080
#define BIF_SDCSR_TB2	0x00000040
#define BIF_SDCSR_TB1	0x00000020
#define BIF_SDCSR_RB	0x0000001c	/* data in receive FIFO					*/
#define BIF_SDCSR_RB4	0x00000010
#define BIF_SDCSR_RB2	0x00000008	
#define BIF_SDCSR_RB1	0x00000004	
#define BIF_SDCSR_DE	0x00000002	/* DMA interface enable bitr			*/
#define BIF_SDCSR_DR	0x00000001	/* data transfer direction				*/

#define BIF_RDCSR_ER    BIF_SDCSR_ER    /* error  detected by BIF */
#define BIF_RDCSR_PE	BIF_SDCSR_PE	/* detect parity error in sync		*/
#define BIF_RDCSR_SP	BIF_SDCSR_SP	/* parity error in sync	  */
#define BIF_RDCSR_LP	BIF_SDCSR_LP	/* L-bus parity error	  */
#define BIF_RDCSR_LR	BIF_SDCSR_LR	/* */
#define BIF_RDCSR_LW	BIF_SDCSR_LW	/* */
#define BIF_RDCSR_AL	BIF_SDCSR_AL	/* specify end bit except of end data */
#define BIF_RDCSR_SS	BIF_SDCSR_SS	/* request bit but masked by slow sync	*/
#define BIF_RDCSR_SC	BIF_SDCSR_SC	/* clear bit but masked by slow sync  */
#define BIF_RDCSR_SY	BIF_SDCSR_SY	/* set bit but masked by slow status  */
#define BIF_RDCSR_FS	BIF_SDCSR_FS	/* request bit but masked by fast sync*/
#define BIF_RDCSR_FC	BIF_SDCSR_FC	/* clear bit but masked by fast sync  */
#define BIF_RDCSR_FY	BIF_SDCSR_FY	/* set bit but masked by fast status  */
#define BIF_RDCSR_CP	BIF_SDCSR_CP	/* parity error in commnad bus        */
#define BIF_RDCSR_FP	BIF_SDCSR_FP	/* execute scatter or gather but FN=0 */
#define BIF_RDCSR_PS	BIF_SDCSR_PS	/* header receive error 			*/
#define BIF_RDCSR_RA	BIF_SDCSR_RA	/* change scatter,gather parameter		*/
#define BIF_RDCSR_DL	BIF_SDCSR_DL	/* lost data					*/
#define BIF_RDCSR_PA	BIF_SDCSR_PA	/* check if send or receive error	  */
#define BIF_RDCSR_BB	BIF_SDCSR_BB	/* check if some BIF use command bus  */
#define BIF_RDCSR_BG	BIF_SDCSR_BG	/* check if command bus got		*/
#define BIF_RDCSR_BR	BIF_SDCSR_BR	/* request command bus			*/
#define BIF_RDCSR_CN	BIF_SDCSR_CN	/* release BIF from command bus	*/
#define BIF_RDCSR_EB	BIF_SDCSR_EB	/* send data that have end bit	*/
#define BIF_RDCSR_TB	BIF_SDCSR_TB	/* data in send FIFO			*/
#define BIF_RDCSR_RB	BIF_SDCSR_RB	/* data in receive FIFO			*/
#define BIF_RDCSR_DE	BIF_SDCSR_DE	/* DMA interface enable bitr	*/
#define BIF_RDCSR_DR	BIF_SDCSR_DR	/* data transfer direction		*/
#define BIF_RDCSR_FN	BIF_SDCSR_FN	/* scatter gather enable		*/

#define BIF_MHOCR_RS	0x00000800		/* bif reset						*/
#define BIF_MHOCR_RC	0x00000400		/* commnad bus circuit reset		*/
#define BIF_MHOCR_RI	0x00000200		/* remove input buffer data			*/
#define BIF_MHOCR_RO	0x00000100		/* remove output buffer data		*/
#define BIF_MHOCR_BA	0x00000008		/* command bus arbitlater reset		*/
#define BIF_MHOCR_MD	0x00000006		/* command bus mode					*/
#define BIF_MHOCR_AT	0x00000001		/* command bus attention signal		*/

#define BIF_MHOCR_MD_NORMAL	0x00000006	/* command bus mode [normal]		*/
#define BIF_MHOCR_MD_BUSWGR	0x00000004	/* command bus mode [bus gather]	*/
#define BIF_MHOCR_MD_SETCID	0x00000002	/* command bus mode [set cid]		*/


