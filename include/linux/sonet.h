/* sonet.h - SONET/SHD physical layer control */
 
/* Written 1995 by Werner Almesberger, EPFL LRC */
 

#ifndef LINUX_SONET_H
#define LINUX_SONET_H

struct sonet_stats {
	long section_bip;		/* section parity errors (B1) */
	long line_bip;			/* line parity errors (B2) */
	long path_bip;			/* path parity errors (B3) */
	long line_febe;			/* line parity errors at remote */
	long path_febe;			/* path parity errors at remote */
	long corr_hcs;			/* correctable header errors */
	long uncorr_hcs;		/* uncorrectable header errors */
	long tx_cells;			/* cells sent */
	long rx_cells;			/* cells received */
};

#define SONET_GETSTAT	_IOR('a',ATMIOC_PHYTYP,struct sonet_stats)
					/* get statistics */
#define SONET_GETSTATZ	_IOR('a',ATMIOC_PHYTYP+1,struct sonet_stats)
					/* ... and zero counters */
#define SONET_SETDIAG	_IOWR('a',ATMIOC_PHYTYP+2,int)
					/* set error insertion */
#define SONET_CLRDIAG	_IOWR('a',ATMIOC_PHYTYP+3,int)
					/* clear error insertion */
#define SONET_GETDIAG	_IOR('a',ATMIOC_PHYTYP+4,int)
					/* query error insertion */
#define SONET_SETFRAMING _IO('a',ATMIOC_PHYTYP+5)
					/* set framing mode (SONET/SDH) */
#define SONET_GETFRAMING _IOR('a',ATMIOC_PHYTYP+6,int)
					/* get framing mode */
#define SONET_GETFRSENSE _IOR('a',ATMIOC_PHYTYP+7, \
  unsigned char[SONET_FRSENSE_SIZE])	/* get framing sense information */

#define SONET_INS_SBIP	  1		/* section BIP */
#define SONET_INS_LBIP	  2		/* line BIP */
#define SONET_INS_PBIP	  4		/* path BIP */
#define SONET_INS_FRAME	  8		/* out of frame */
#define SONET_INS_LOS	 16		/* set line to zero */
#define SONET_INS_LAIS	 32		/* line alarm indication signal */
#define SONET_INS_PAIS	 64		/* path alarm indication signal */
#define SONET_INS_HCS	128		/* insert HCS error */

#define SONET_FRAME_SONET 0		/* SONET STS-3 framing */
#define SONET_FRAME_SDH   1		/* SDH STM-1 framing */

#define SONET_FRSENSE_SIZE 6		/* C1[3],H1[3] (0xff for unknown) */

#endif
