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
         u_long         dmacnva;        /* Capture Next VAddress */
         u_long         dmacnc;         /* Capture Next Count */
         u_long         dmapva;         /* Playback Virtual Address */
         u_long         dmapc;          /* Playback Count */
         u_long         dmapnva;        /* Playback Next VAddress */
         u_long         dmapnc;         /* Playback Next Count */
};

struct cs4231_chip {
        struct cs4231_regs pioregs;
        struct cs4231_dma dmaregs;
};

struct cs4231_stream_info {
        unsigned int sample_rate;     /* samples per second */
        unsigned int channels;        /* number of interleaved channels */
        unsigned int precision;       /* bit-width of each sample */
        unsigned int encoding;        /* data encoding method */
        unsigned int gain;            /* gain level: 0 - 255 */
        unsigned int port;
};

#define CS_TIMEOUT      9000000

#define GAIN_SET(var, gain)     ((var & ~(0x3f)) | gain)
#define RECGAIN_SET(var, gain)  ((var & ~(0x1f)) | gain)

#define IAR_AUTOCAL_BEGIN       0x40
#define IAR_AUTOCAL_END         ~(0x40)
#define IAR_NOT_READY            0x80    /* 80h not ready CODEC state */

#define MIC_ENABLE(var)         ((var & 0x2f) | 0x80)
#define LINE_ENABLE(var)        (var & 0x2f)
#define CDROM_ENABLE(var)       ((var & 0x2f) | 0x40)

#define OUTCR_MUTE              0x80
#define OUTCR_UNMUTE            ~0x80

/* 8 */
#define DEFAULT_DATA_FMAT               0x20

/* 10 */
#define PINCR_LINE_MUTE         0x40
#define PINCR_HDPH_MUTE         0x80

/* 11 */
#define AUTOCAL_IN_PROGRESS      0x20

/* 12 */
#define MISC_IR_MODE2           0x40

/* 13 */
#define LOOPB_ON                0x01
#define LOOPB_OFF               0x00

/* 16 */
#define OLB_ENABLE              0x80

/* 17 */
#define HPF_ON                  0x01
#define XTALE_ON                0x20

#define MONO_IOCR_MUTE       0x40;

/* 30 */
#define CS4231A         0x20


#define APC_CODEC_PDN   0x20            
#define APC_RESET       0x01            

#define CS4231_DEFAULT_PLAYGAIN     (132)
#define CS4231_DEFAULT_RECGAIN      (126)

#define CS4231_MIN_ATEN     (0)
#define CS4231_MAX_ATEN     (31)
#define CS4231_MAX_DEV_ATEN (63)
#define CS4231_MIN_GAIN     (0)
#define CS4231_MAX_GAIN     (15)
#define CS4231_MON_MIN_ATEN         (0)
#define CS4231_MON_MAX_ATEN         (63)

#define CS4231_PRECISION    (8)             /* Bits per sample unit */
#define CS4231_CHANNELS     (1)             /* Channels per sample frame */

#define CS4231_RATE   (8000)          

#define AUDIO_ENCODING_NONE     (0) /* no encoding assigned */
#define AUDIO_ENCODING_ULAW     (1) /* u-law encoding */
#define AUDIO_ENCODING_ALAW     (2) /* A-law encoding */
#define AUDIO_ENCODING_LINEAR   (3) /* Linear PCM encoding */
#define AUDIO_ENCODING_DVI      (104) /* DVI ADPCM */
#define AUDIO_ENCODING_LINEAR8  (105) /* 8 bit UNSIGNED */

#define AUDIO_LEFT_BALANCE      (0)
#define AUDIO_MID_BALANCE       (32)
#define AUDIO_RIGHT_BALANCE     (64)
#define AUDIO_BALANCE_SHIFT     (3)

#define AUDIO_SPEAKER           0x01
#define AUDIO_HEADPHONE         0x02
#define AUDIO_LINE_OUT          0x04

#define AUDIO_MICROPHONE        0x01
#define AUDIO_LINE_IN           0x02
#define AUDIO_INTERNAL_CD_IN    0x04

#define AUDIO_MAX_GAIN  (255)

#endif
