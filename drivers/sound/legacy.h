#ifndef _SOUND_LEGACY_H_
#define _SOUND_LEGACY_H_

/*
 *	Force on additional support
 */

#define __SGNXPRO__
#define DESKPROXL
/* #define SM_GAMES */
#define SM_WAVE

/*
 * Define legacy options.
 */

#define SELECTED_SOUND_OPTIONS		0x0

#define HAVE_MAUI_BOOT
#define PSS_HAVE_LD
#define INCLUDE_TRIX_BOOT

#define CONFIG_CS4232
#define CONFIG_GUS
#define CONFIG_MAD16
#define CONFIG_MAUI
#define CONFIG_MPU401
#define CONFIG_MSS
#define CONFIG_OPL3SA1
#define CONFIG_OPL3SA2
#define CONFIG_PAS
#define CONFIG_PSS
#define CONFIG_SB
#define CONFIG_SOFTOSS
#define CONFIG_SSCAPE
#define CONFIG_AD1816
#define CONFIG_TRIX
#define CONFIG_VMIDI
#define CONFIG_YM3812

#define CONFIG_AUDIO
#define CONFIG_MIDI
#define CONFIG_SEQUENCER

#define CONFIG_AD1848
#define CONFIG_MPU_EMU
#define CONFIG_SBDSP
#define CONFIG_UART401

#endif	/* _SOUND_LEGACY_H */
