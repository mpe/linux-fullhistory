/*
 * drivers/sbus/audio/cs4231.h
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 * Copyright (C) 1997 Derrick J. Brashear (shadow@dementia.org)
 */

#ifndef _CS4231_H_
#define _CS4231_H_

#include <linux/types.h>

struct cs4231_regs {
         u_char iar;            /* Index Address Register */
         u_char pad0[3];         
         u_char idr;            /* Indexed Data Register */
         u_char pad1[3];        
         u_char statr;          /* Status Register */
         u_char pad2[3];        
         u_char piodr;          /* PIO Data Register I/O */
         u_char pad3[3];        
};

struct cs4231_dma {
         u_long         dmacsr;         /* APC CSR */
         u_long         dmapad[3];        
         u_long         dmacva;         /* Capture Virtual Address */
         u_long         dmacc;          /* Capture Count */
         u_long         dmacnva;        /* Capture Next Virtual Address */
         u_long         dmacnc;         /* Capture Next Count */
         u_long         dmapva;         /* Playback Virtual Address */
         u_long         dmapc;          /* Playback Count */
         u_long         dmapnva;        /* Playback Next Virtual Address */
         u_long         dmapnc;         /* Playback Next Count */
};

struct cs4231_chip {
        struct cs4231_regs *pioregs;
        struct cs4231_dma dmaregs;
	struct audio_info perchip_info;
	int irq;
	unsigned long regs_size;

	/* Keep track of various info */
	volatile unsigned int status;

        int dma;
        int dma2;
};

/* Status bits */
#define CS_STATUS_NEED_INIT 0x01
#define CS_STATUS_INIT_ON_CLOSE 0x02
#define CS_STATUS_REV_A 0x04

#define CS_TIMEOUT      9000000

#define GAIN_SET(var, gain)     ((var & ~(0x3f)) | gain)
#define RECGAIN_SET(var, gain)  ((var & ~(0x1f)) | gain)

#define IAR_AUTOCAL_BEGIN       0x40 /* IAR_MCE */
#define IAR_AUTOCAL_END         ~(0x40) /* IAR_MCD */
#define IAR_NOT_READY            0x80    /* 80h not ready CODEC state */

/* Each register assumed mode 1 and 2 unless noted */

/* 0 - Left Input Control */
/* 1 - Right Input Control */
#define MIC_ENABLE(var)         ((var & 0x2f) | 0x80)
#define LINE_ENABLE(var)        (var & 0x2f)
#define CDROM_ENABLE(var)       ((var & 0x2f) | 0x40)
#define INPUTCR_AUX1            0x40

/* 2 - Left Aux 1 Input Control */
/* 3 - Right Aux 1 Input Control */
/* 4 - Left Aux 2 Input Control */
/* 5 - Right Aux 2 Input Control */

/* 6 - Left Output Control */
/* 7 - Right Output Control */
#define OUTCR_MUTE              0x80
#define OUTCR_UNMUTE            ~0x80

/* 8 - Playback Data Format (Mode 2) */
#define CHANGE_DFR(var, val)            ((var & ~(0xF)) | val)
#define CHANGE_ENCODING(var, val)       ((var & ~(0xe0)) | val)
#define DEFAULT_DATA_FMAT               CS4231_DFR_ULAW
#define CS4231_DFR_8000                 0x00
#define CS4231_DFR_9600                 0x0e
#define CS4231_DFR_11025                0x03
#define CS4231_DFR_16000                0x02
#define CS4231_DFR_18900                0x05
#define CS4231_DFR_22050                0x07
#define CS4231_DFR_32000                0x06
#define CS4231_DFR_37800                0x09
#define CS4231_DFR_44100                0x0b
#define CS4231_DFR_48000                0x0c
#define CS4231_DFR_LINEAR8              0x00
#define CS4231_DFR_ULAW                 0x20
#define CS4231_DFR_ALAW                 0x60
#define CS4231_DFR_ADPCM                0xa0
#define CS4231_DFR_LINEARBE             0xc0
#define CS4231_STEREO_ON(val)           (val | 0x10)
#define CS4231_MONO_ON(val)             (val & ~0x10)

/* 9 - Interface Config. Register */
#define CHIP_INACTIVE           0x08
#define PEN_ENABLE              (0x01)
#define PEN_DISABLE             (~0x01)
#define CEN_ENABLE              (0x02)
#define CEN_DISABLE             (~0x02)
#define ACAL_DISABLE            (~0x08)
#define ICR_AUTOCAL_INIT        0x01

/* 10 - Pin Control Register */
#define INTR_ON                 0x82
#define INTR_OFF                0x80
#define PINCR_LINE_MUTE         0x40
#define PINCR_HDPH_MUTE         0x80

/* 11 - Test/Initialization */
#define DRQ_STAT                0x10
#define AUTOCAL_IN_PROGRESS      0x20

/* 12 - Misc Information */
#define MISC_IR_MODE2           0x40

/* 13 - Loopback Control */
#define LOOPB_ON                0x01
#define LOOPB_OFF               0x00

/* 14 - Unused (mode 1) */
/* 15 - Unused (mode 1) */

/* 14 - Playback Upper (mode 2) */
/* 15 - Playback Lower (mode 2) */

/* The rest are mode 2 only */

/* 16 - Alternate Feature 1 Enable */
#define OLB_ENABLE              0x80

/* 17 - Alternate Feature 2 Enable */
#define HPF_ON                  0x01
#define XTALE_ON                0x20

/* 18 - Left Line Input Gain */
/* 19 - Right Line Input Gain */

/* 20 - Timer High */
/* 21 - Timer Low */

/* 22 - unused */
/* 23 - unused */

/* 24 - Alternate Feature Status */
#define CS_PU                   0x01 /* Underrun */
#define CS_PO                   0x20 /* Overrun */

/* 25 - Version */
#define CS4231A         0x20
#define CS4231CDE       0x80

/* 26 - Mono I/O Control */
#define CHANGE_MONO_GAIN(val)   ((val & ~(0xFF)) | val)
#define MONO_IOCR_MUTE       0x40

/* 27 - Unused */

/* 28 - Capture Data Format */
/* see register 8 */

/* 29 - Unused */

/* 30 - Capture Upper */
/* 31 - Capture Lower */

/* Following are CSR register definitions for the Sparc */
/* Also list "Solaris" equivs for now, not really useful tho */
#define CS_INT_PENDING 0x800000 /* APC_IP */ /* Interrupt Pending */
#define CS_PLAY_INT    0x400000 /* APC_PI */ /* Playback interrupt */
#define CS_CAPT_INT    0x200000 /* APC_CI */ /* Capture interrupt */
#define CS_GENL_INT    0x100000 /* APC_EI */ /* General interrupt */
#define CS_XINT_ENA    0x80000  /* APC_IE */ /* General ext int. enable */
#define CS_XINT_PLAY   0x40000  /* APC_PIE */ /* Playback ext intr */
#define CS_XINT_CAPT   0x20000  /* APC_CIE */ /* Capture ext intr */
#define CS_XINT_GENL   0x10000  /* APC_EIE */ /* Error ext intr */
#define CS_XINT_EMPT   0x8000   /* APC_PMI */ /* Pipe empty interrupt */
#define CS_XINT_PEMP   0x4000   /* APC_PM */ /* Play pipe empty */
#define CS_XINT_PNVA   0x2000   /* APC_PD */ /* Playback NVA dirty */
#define CS_XINT_PENA   0x1000   /* APC_PMIE */ /* play pipe empty Int enable */
#define CS_XINT_COVF   0x800    /* APC_CM */ /* Cap data dropped on floor */
#define CS_XINT_CNVA   0x400    /* APC_CD */ /* Capture NVA dirty */
#define CS_XINT_CEMP   0x200    /* APC_CMI */ /* Capture pipe empty interrupt */
#define CS_XINT_CENA   0x100    /* APC_CMIE */ /* Cap. pipe empty int enable */
#define CS_PPAUSE      0x80     /* APC_PPAUSE */ /* Pause the play DMA */
#define CS_CPAUSE      0x40     /* APC_CPAUSE */ /* Pause the capture DMA */
#define CS_CDC_RESET   0x20     /* APC_CODEC_PDN */ /* CODEC RESET */
#define PDMA_READY     0x08     /* PDMA_GO */
#define CDMA_READY     0x04     /* CDMA_GO */
#define CS_CHIP_RESET  0x01     /* APC_RESET */       /* Reset the chip */

#define CS_INIT_SETUP  (CDMA_READY | PDMA_READY | CS_XINT_ENA | CS_XINT_PLAY | CS_XINT_GENL | CS_INT_PENDING | CS_PLAY_INT | CS_CAPT_INT | CS_GENL_INT) 

#define CS_PLAY_SETUP  (CS_GENL_INT | CS_PLAY_INT | CS_XINT_ENA | CS_XINT_PLAY | CS_XINT_EMPT | CS_XINT_GENL | CS_XINT_PENA | PDMA_READY)

#define CS_CAPT_SETUP  (CS_GENL_INT | CS_CAPT_INT | CS_XINT_ENA | CS_XINT_CAPT | CS_XINT_CEMP | CS_XINT_GENL | CDMA_READY)

#define CS4231_MIN_ATEN     (0)
#define CS4231_MAX_ATEN     (31)
#define CS4231_MAX_DEV_ATEN (63)

#define CS4231_MON_MIN_ATEN         (0)
#define CS4231_MON_MAX_ATEN         (63)

#define CS4231_DEFAULT_PLAYGAIN     (132)
#define CS4231_DEFAULT_RECGAIN      (126)

#define CS4231_MIN_GAIN     (0)
#define CS4231_MAX_GAIN     (15)

#define CS4231_PRECISION    (8)             /* # of bits/sample */
#define CS4231_CHANNELS     (1)             /* channels/sample */

#define CS4231_RATE   (8000)                /* default sample rate */
/* Other rates supported are:
 * 9600, 11025, 16000, 18900, 22050, 32000, 37800, 44100, 48000 
 */

#endif
