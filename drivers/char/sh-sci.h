/* $Id: sh-sci.h,v 1.5 2000-03-05 13:54:32+09 gniibe Exp $
 *
 *  linux/drivers/char/sh-sci.h
 *
 *  SuperH on-chip serial module support.  (SCI with no FIFO / with FIFO)
 *  Copyright (C) 1999, 2000  Niibe Yutaka
 *  Copyright (C) 2000  Greg Banks
 *
 */

#if defined(CONFIG_SH_SCI_SERIAL)
#if defined(__sh3__)
#define SCSMR  (volatile unsigned char *)0xfffffe80
#define SCBRR  0xfffffe82
#define SCSCR  (volatile unsigned char *)0xfffffe84
#define SC_TDR  0xfffffe86
#define SC_SR  (volatile unsigned char *)0xfffffe88
#define SC_RDR  0xfffffe8a
#define SCSPTR 0xffffff7c

#define SCSCR_INIT	0x30	/* TIE=0,RIE=0,TE=1,RE=1 */

#elif defined(__SH4__)
Not yet.
#endif

#define SCI_TD_E  0x80
#define SCI_RD_F  0x40
#define SCI_ORER  0x20
#define SCI_FER   0x10
#define SCI_PER   0x08
#define SCI_TEND  0x04

#define SCI_ERRORS ( SCI_PER | SCI_FER | SCI_ORER)
#define SCI_TD_E_CLEAR		0x78
#define SCI_RDRF_CLEAR		0xbc
#define SCI_ERROR_CLEAR		0xc4

#define SCI_CTRL_FLAGS_TIE  0x80
#define SCI_CTRL_FLAGS_RIE  0x40
#define SCI_CTRL_FLAGS_TE   0x20
#define SCI_CTRL_FLAGS_RE   0x10
/* TEIE=0x04 */
#define SCI_CTRL_FLAGS_CKE1 0x02
#define SCI_CTRL_FLAGS_CKE0 0x01

#define RFCR    0xffffff74

#define SCI_ERI_IRQ	23
#define SCI_RXI_IRQ	24
#define SCI_TXI_IRQ	25
#define SCI_TEI_IRQ	26
#define SCI_IRQ_END	27

#define SCI_IPR_OFFSET	(16+4)
#endif

#if defined(CONFIG_SH_SCIF_SERIAL)
#if defined(__sh3__)
#define SCSMR  (volatile unsigned char *)0xA4000150
#define SCBRR  0xA4000152
#define SCSCR  (volatile unsigned char *)0xA4000154
#define SC_TDR 0xA4000156
#define SC_SR  (volatile unsigned short *)0xA4000158
#define SC_RDR 0xA400015A
#define SCFCR  (volatile unsigned char *)0xA400015C
#define SCFDR  0xA400015E
#undef  SCSPTR /* Is there any register for RTS?? */
#undef  SCLSR

#define RFCR   0xffffff74

#define SCSCR_INIT	0x30	/* TIE=0,RIE=0,TE=1,RE=1 */
				/* 0x33 when external clock is used */
#define SCI_IPR_OFFSET	(64+4)

#elif defined(__SH4__)
#define SCSMR  (volatile unsigned short *)0xFFE80000
#define SCBRR  0xFFE80004
#define SCSCR  (volatile unsigned short *)0xFFE80008
#define SC_TDR 0xFFE8000C
#define SC_SR  (volatile unsigned short *)0xFFE80010
#define SC_RDR 0xFFE80014
#define SCFCR  (volatile unsigned short *)0xFFE80018
#define SCFDR  0xFFE8001C
#define SCSPTR 0xFFE80020
#define SCLSR  0xFFE80024

#define RFCR   0xFF800028

#define SCSCR_INIT	0x0038	/* TIE=0,RIE=0,TE=1,RE=1,REIE=1 */
#define SCI_IPR_OFFSET	(32+4)

#endif

#define SCI_ER    0x0080
#define SCI_TEND  0x0040
#define SCI_TD_E  0x0020
#define SCI_BRK   0x0010
#define SCI_FER   0x0008
#define SCI_PER   0x0004
#define SCI_RD_F  0x0002
#define SCI_DR    0x0001

#define SCI_ERRORS ( SCI_PER | SCI_FER | SCI_ER | SCI_BRK)
#define SCI_TD_E_CLEAR		0x00df
#define SCI_TEND_CLEAR		0x00bf
#define SCI_RDRF_CLEAR		0x00fc
#define SCI_ERROR_CLEAR		0x0063

#define SCI_CTRL_FLAGS_TIE  0x80
#define SCI_CTRL_FLAGS_RIE  0x40
#define SCI_CTRL_FLAGS_TE   0x20
#define SCI_CTRL_FLAGS_RE   0x10
#define SCI_CTRL_FLAGS_REIE 0x08
#define SCI_CTRL_FLAGS_CKE1 0x02

#if defined(__sh3__)
#define SCI_ERI_IRQ	56
#define SCI_RXI_IRQ	57
#define SCI_BRI_IRQ	58
#define SCI_TXI_IRQ	59
#define SCI_IRQ_END	60
#elif defined(__SH4__)
#define SCI_ERI_IRQ	40
#define SCI_RXI_IRQ	41
#define SCI_BRI_IRQ	42
#define SCI_TXI_IRQ	43
#define SCI_IRQ_END	44
#endif
#endif

#define SCI_PRIORITY	3

#define SCI_MINOR_START		64
#define SCI_RX_THROTTLE		0x0000001

#define O_OTHER(tty)    \
      ((O_OLCUC(tty))  ||\
      (O_ONLCR(tty))   ||\
      (O_OCRNL(tty))   ||\
      (O_ONOCR(tty))   ||\
      (O_ONLRET(tty))  ||\
      (O_OFILL(tty))   ||\
      (O_OFDEL(tty))   ||\
      (O_NLDLY(tty))   ||\
      (O_CRDLY(tty))   ||\
      (O_TABDLY(tty))  ||\
      (O_BSDLY(tty))   ||\
      (O_VTDLY(tty))   ||\
      (O_FFDLY(tty)))

#define I_OTHER(tty)    \
      ((I_INLCR(tty))  ||\
      (I_IGNCR(tty))   ||\
      (I_ICRNL(tty))   ||\
      (I_IUCLC(tty))   ||\
      (L_ISIG(tty)))

#define SCI_MAGIC 0xbabeface

struct sci_port {
	struct gs_port gs;
	unsigned int old_cflag;
};

#define WAIT_RFCR_COUNTER 200

/*
 * Values for the BitRate Register (SCBRR)
 *
 * The values are actually divisors for a frequency which can
 * be internal to the SH3 (14.7456MHz) or derived from an external
 * clock source.  This driver assumes the internal clock is used;
 * to support using an external clock source, config options or
 * possibly command-line options would need to be added.
 *
 * Also, to support speeds below 2400 (why?) the lower 2 bits of
 * the SCSMR register would also need to be set to non-zero values.
 *
 * -- Greg Banks 27Feb2000
 */

#if defined(__sh3__)
#define BPS_2400       191
#define BPS_4800       95
#define BPS_9600       47
#define BPS_19200      23
#define BPS_38400      11
#define BPS_115200     3
#elif defined(__SH4__)
/* Values for SH-4 please! */

#define BPS_115200     8
#endif
