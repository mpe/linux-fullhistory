/*
 * ac97.h 
 * 
 * definitions for the AC97, Intel's Audio Codec 97 Spec
 */

#ifndef _AC97_H_
#define _AC97_H_

                                             // conections on concert 97 */
#define  AC97_RESET              0x0000      //  */
#define  AC97_MASTER_VOL_STEREO  0x0002      // Line Out
#define  AC97_HEADPHONE_VOL      0x0004      // 
#define  AC97_MASTER_VOL_MONO    0x0006      // TAD Output
#define  AC97_MASTER_TONE        0x0008      //
#define  AC97_PCBEEP_VOL         0x000a      // none
#define  AC97_PHONE_VOL          0x000c      // TAD Input (mono)
#define  AC97_MIC_VOL            0x000e      // MIC Input (mono)
#define  AC97_LINEIN_VOL         0x0010      // Line Input (stereo)
#define  AC97_CD_VOL             0x0012      // CD Input (stereo)
#define  AC97_VIDEO_VOL          0x0014      // none
#define  AC97_AUX_VOL            0x0016      // Aux Input (stereo)
#define  AC97_PCMOUT_VOL         0x0018      // Wave Output (stereo)
#define  AC97_RECORD_SELECT      0x001a      //
#define  AC97_RECORD_GAIN        0x001c
#define  AC97_RECORD_GAIN_MIC    0x001e
#define  AC97_GENERAL_PURPOSE    0x0020
#define  AC97_3D_CONTROL         0x0022
#define  AC97_MODEM_RATE         0x0024
#define  AC97_POWER_CONTROL      0x0026

/* registers 0x0028 - 0x0058 are reserved */

/* registers 0x005a - 0x007a are vendor reserved */

#define  AC97_VENDOR_ID1         0x007c
#define  AC97_VENDOR_ID2         0x007e


/* volume control bit defines */

#define AC97_MUTE                0x8000
#define AC97_MICBOOST            0x0040
#define AC97_LEFTVOL             0x3f00
#define AC97_RIGHTVOL            0x003f

/* record mux defines */

#define AC97_RECMUX_MIC          0x0000
#define AC97_RECMUX_CD           0x0101
#define AC97_RECMUX_VIDEO        0x0202      /* not used */
#define AC97_RECMUX_AUX          0x0303      
#define AC97_RECMUX_LINE         0x0404      
#define AC97_RECMUX_STEREO_MIX   0x0505
#define AC97_RECMUX_MONO_MIX     0x0606
#define AC97_RECMUX_PHONE        0x0707


/* general purpose register bit defines */

#define AC97_GP_LPBK             0x0080      /* Loopback mode */
#define AC97_GP_MS               0x0100      /* Mic Select 0=Mic1, 1=Mic2 */
#define AC97_GP_MIX              0x0200      /* Mono output select 0=Mix, 1=Mic */
#define AC97_GP_RLBK             0x0400      /* Remote Loopback - Modem line codec */
#define AC97_GP_LLBK             0x0800      /* Local Loopback - Modem Line codec */
#define AC97_GP_LD               0x1000      /* Loudness 1=on */
#define AC97_GP_3D               0x2000      /* 3D Enhancement 1=on */
#define AC97_GP_ST               0x4000      /* Stereo Enhancement 1=on */
#define AC97_GP_POP              0x8000      /* Pcm Out Path, 0=pre 3D, 1=post 3D */


/* powerdown control and status bit defines */

/* status */
#define AC97_PWR_MDM             0x0010      /* Modem section ready */
#define AC97_PWR_REF             0x0008      /* Vref nominal */
#define AC97_PWR_ANL             0x0004      /* Analog section ready */
#define AC97_PWR_DAC             0x0002      /* DAC section ready */
#define AC97_PWR_ADC             0x0001      /* ADC section ready */

/* control */
#define AC97_PWR_PR0             0x0100      /* ADC and Mux powerdown */
#define AC97_PWR_PR1             0x0200      /* DAC powerdown */
#define AC97_PWR_PR2             0x0400      /* Output mixer powerdown (Vref on) */
#define AC97_PWR_PR3             0x0800      /* Output mixer powerdown (Vref off) */
#define AC97_PWR_PR4             0x1000      /* AC-link powerdown */
#define AC97_PWR_PR5             0x2000      /* Internal Clk disable */
#define AC97_PWR_PR6             0x4000      /* HP amp powerdown */
#define AC97_PWR_PR7             0x8000      /* Modem off - if supported */

/* useful power states */
#define AC97_PWR_D0              0x0000      /* everything on */
#define AC97_PWR_D1              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR4
#define AC97_PWR_D2              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR2|AC97_PWR_PR3|AC97_PWR_PR4
#define AC97_PWR_D3              AC97_PWR_PR0|AC97_PWR_PR1|AC97_PWR_PR2|AC97_PWR_PR3|AC97_PWR_PR4
#define AC97_PWR_ANLOFF          AC97_PWR_PR2|AC97_PWR_PR3  /* analog section off */

#endif /* _AC97_H_ */



