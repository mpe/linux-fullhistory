/* 
 * arcaudio.h
 *
 */

#ifndef _LINUX_ARCAUDIO_H
#define _LINUX_ARCAUDIO_H

#define ARCAUDIO_MAXCHANNELS	8

enum ch_type
{
  ARCAUDIO_NONE,			/* No sound (muted) */
  ARCAUDIO_8BITSIGNED,			/* signed 8 bits per samples */
  ARCAUDIO_8BITUNSIGNED,		/* unsigned 8 bits per samples */
  ARCAUDIO_16BITSIGNED,			/* signed 16 bits per samples (little endian) */
  ARCAUDIO_16BITUNSIGNED,		/* unsigned 16 bits per samples (little endian) */
  ARCAUDIO_LOG				/* Vidc Log */
};

/* 
 * Global information
 */
struct arcaudio
{
  int		sample_rate;		/* sample rate (Hz) */
  int		num_channels;		/* number of channels */
  int		volume;			/* overall system volume */
};

/* 
 * Per channel information
 */
struct arcaudio_channel
{
  int		stereo_position;	/* Channel position */
  int		channel_volume;		/* Channel volume */
  enum ch_type	channel_type;		/* Type of channel */
  int		buffer_size;		/* Size of channel buffer */
};

/* IOCTLS */
#define ARCAUDIO_GETINFO	0x6101
#define ARCAUDIO_SETINFO	0x6102
#define ARCAUDIO_GETCHANNELINFO	0x6111
#define ARCAUDIO_SETCHANNELINFO	0x6112
#define ARCAUDIO_GETOPTS	0x61f0
#define ARCAUDIO_SETOPTS	0x61f1
#define  ARCAUDIO_OPTSPKR	1<<0

#endif
