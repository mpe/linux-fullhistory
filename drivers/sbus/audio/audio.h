/*
 * drivers/sbus/audio/audio.h
 *
 * Sparc Audio Midlayer
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 */

#ifndef _AUDIO_H_
#define _AUDIO_H_

/*
 *	SunOS/Solaris /dev/audio interface
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/ioctl.h>

/*
 * This structure contains state information for audio device IO streams.
 */
typedef struct audio_prinfo {
	/*
	 * The following values describe the audio data encoding.
	 */
	unsigned int sample_rate;	/* samples per second */
	unsigned int channels;	/* number of interleaved channels */
	unsigned int precision;	/* bit-width of each sample */
	unsigned int encoding;	/* data encoding method */

	/*
	 * The following values control audio device configuration
	 */
	unsigned int gain;		/* gain level: 0 - 255 */
	unsigned int port;		/* selected I/O port (see below) */
	unsigned int avail_ports;	/* available I/O ports (see below) */
	unsigned int _xxx[2];		/* Reserved for future use */

	unsigned int buffer_size;	/* I/O buffer size */

	/*
	 * The following values describe driver state
	 */
	unsigned int samples;		/* number of samples converted */
	unsigned int eof;		/* End Of File counter (play only) */

	unsigned char	pause;		/* non-zero for pause, zero to resume */
	unsigned char	error;		/* non-zero if overflow/underflow */
	unsigned char	waiting;	/* non-zero if a process wants access */
	unsigned char balance;	/* stereo channel balance */

	unsigned short minordev;

	/*
	 * The following values are read-only state flags
	 */
	unsigned char open;		/* non-zero if open access permitted */
	unsigned char active;		/* non-zero if I/O is active */
} audio_prinfo_t;


/*
 * This structure describes the current state of the audio device.
 */
typedef struct audio_info {
	/*
	 * Per-stream information
	 */
	audio_prinfo_t play;	/* output status information */
	audio_prinfo_t record;	/* input status information */

	/*
	 * Per-unit/channel information
	 */
	unsigned int monitor_gain;	/* input to output mix: 0 - 255 */
	unsigned char output_muted;	/* non-zero if output is muted */
	unsigned char _xxx[3];	/* Reserved for future use */
	unsigned int _yyy[3];		/* Reserved for future use */
} audio_info_t;


/*
 * Audio encoding types
 */
#define	AUDIO_ENCODING_NONE	(0)	/* no encoding assigned	*/
#define	AUDIO_ENCODING_ULAW	(1)	/* u-law encoding	*/
#define	AUDIO_ENCODING_ALAW	(2)	/* A-law encoding	*/
#define	AUDIO_ENCODING_LINEAR	(3)	/* Linear PCM encoding	*/
#define	AUDIO_ENCODING_DVI	(104)	/* DVI ADPCM		*/
#define	AUDIO_ENCODING_LINEAR8	(105)	/* 8 bit UNSIGNED	*/

/*
 * These ranges apply to record, play, and monitor gain values
 */
#define	AUDIO_MIN_GAIN	(0)	/* minimum gain value */
#define	AUDIO_MAX_GAIN	(255)	/* maximum gain value */

/*
 * These values apply to the balance field to adjust channel gain values
 */
#define	AUDIO_LEFT_BALANCE	(0)	/* left channel only	*/
#define	AUDIO_MID_BALANCE	(32)	/* equal left/right channel */
#define	AUDIO_RIGHT_BALANCE	(64)	/* right channel only	*/
#define	AUDIO_BALANCE_SHIFT	(3)

/*
 * Generic minimum/maximum limits for number of channels, both modes
 */
#define	AUDIO_MIN_PLAY_CHANNELS	(1)
#define	AUDIO_MAX_PLAY_CHANNELS	(4)
#define	AUDIO_MIN_REC_CHANNELS	(1)
#define	AUDIO_MAX_REC_CHANNELS	(4)

/*
 * Generic minimum/maximum limits for sample precision
 */
#define	AUDIO_MIN_PLAY_PRECISION	(8)
#define	AUDIO_MAX_PLAY_PRECISION	(32)
#define	AUDIO_MIN_REC_PRECISION		(8)
#define	AUDIO_MAX_REC_PRECISION		(32)

/*
 * Define some convenient names for typical audio ports
 */
/*
 * output ports (several may be enabled simultaneously)
 */
#define	AUDIO_SPEAKER		0x01	/* output to built-in speaker */
#define	AUDIO_HEADPHONE		0x02	/* output to headphone jack */
#define	AUDIO_LINE_OUT		0x04	/* output to line out	 */

/*
 * input ports (usually only one at a time)
 */
#define	AUDIO_MICROPHONE	0x01	/* input from microphone */
#define	AUDIO_LINE_IN		0x02	/* input from line in	 */
#define	AUDIO_CD		0x04	/* input from on-board CD inputs */
#define	AUDIO_INTERNAL_CD_IN	AUDIO_CD	/* input from internal CDROM */


/*
 * This macro initializes an audio_info structure to 'harmless' values.
 * Note that (~0) might not be a harmless value for a flag that was
 * a signed int.
 */
#define	AUDIO_INITINFO(i)	{					\
	unsigned int	*__x__;						\
	for (__x__ = (unsigned int *)(i);				\
	    (char *) __x__ < (((char *)(i)) + sizeof (audio_info_t));	\
	    *__x__++ = ~0);						\
}


/*
 * Parameter for the AUDIO_GETDEV ioctl to determine current
 * audio devices.
 */
#define	MAX_AUDIO_DEV_LEN	(16)
typedef struct audio_device {
	char name[MAX_AUDIO_DEV_LEN];
	char version[MAX_AUDIO_DEV_LEN];
	char config[MAX_AUDIO_DEV_LEN];
} audio_device_t;


/*
 * Ioctl calls for the audio device.
 */

/*
 * AUDIO_GETINFO retrieves the current state of the audio device.
 *
 * AUDIO_SETINFO copies all fields of the audio_info structure whose
 * values are not set to the initialized value (-1) to the device state.
 * It performs an implicit AUDIO_GETINFO to return the new state of the
 * device.  Note that the record.samples and play.samples fields are set
 * to the last value before the AUDIO_SETINFO took effect.  This allows
 * an application to reset the counters while atomically retrieving the
 * last value.
 *
 * AUDIO_DRAIN suspends the calling process until the write buffers are
 * empty.
 *
 * AUDIO_GETDEV returns a structure of type audio_device_t which contains
 * three strings.  The string "name" is a short identifying string (for
 * example, the SBus Fcode name string), the string "version" identifies
 * the current version of the device, and the "config" string identifies
 * the specific configuration of the audio stream.  All fields are
 * device-dependent -- see the device specific manual pages for details.
 */
#define	AUDIO_GETINFO	_IOR('A', 1, audio_info_t)
#define	AUDIO_SETINFO	_IOWR('A', 2, audio_info_t)
#define	AUDIO_DRAIN	_IO('A', 3)
#define	AUDIO_GETDEV	_IOR('A', 4, audio_device_t)

/*
 * The following ioctl sets the audio device into an internal loopback mode,
 * if the hardware supports this.  The argument is TRUE to set loopback,
 * FALSE to reset to normal operation.  If the hardware does not support
 * internal loopback, the ioctl should fail with EINVAL.
 */
#define	AUDIO_DIAG_LOOPBACK	_IOW('A', 101, int)

#ifdef notneeded
/*
 * Structure sent up as a M_PROTO message on trace streams
 */
typedef struct audtrace_hdr audtrace_hdr_t;
struct audtrace_hdr {
	unsigned int seq;		/* Sequence number (per-aud_stream) */
	int type;		/* device-dependent */
	struct timeval timestamp;
	char _f[8];		/* filler */
};
#endif



/*
 *	Linux kernel internal implementation.
 */

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/tqueue.h>
#include <linux/wait.h>

#define	SDF_OPEN_WRITE	0x00000001
#define	SDF_OPEN_READ	0x00000002

struct sparcaudio_driver
{
	const char * name;
	struct sparcaudio_operations *ops;
	void *private;
	unsigned long flags;

	/* Processes blocked on open() sit here. */
	struct wait_queue *open_wait;

	/* Task queue for this driver's bottom half. */
	struct tq_struct tqueue;

	/* Support for a circular queue of output buffers. */
	__u8 **output_buffers;
	size_t *output_sizes;
	int num_output_buffers, output_front, output_rear;
	int output_count, output_active;
	struct wait_queue *output_write_wait, *output_drain_wait;
};

struct sparcaudio_operations
{
	int (*open)(struct inode *, struct file *, struct sparcaudio_driver *);
	void (*release)(struct inode *, struct file *, struct sparcaudio_driver *);
	int (*ioctl)(struct inode *, struct file *, unsigned int, unsigned long,
		     struct sparcaudio_driver *);

	/* Ask driver to begin playing a buffer. */
	void (*start_output)(struct sparcaudio_driver *, __u8 *, unsigned long);

	/* Ask driver to stop playing a buffer. */
	void (*stop_output)(struct sparcaudio_driver *);

	/* Ask driver to begin recording into a buffer. */
	void (*start_input)(struct sparcaudio_driver *, __u8 *, unsigned long);

	/* Ask driver to stop recording. */
	void (*stop_input)(struct sparcaudio_driver *);

	/* Return driver name/version to caller. (/dev/audio specific) */
	void (*sunaudio_getdev)(struct sparcaudio_driver *, audio_device_t *);
};

extern int register_sparcaudio_driver(struct sparcaudio_driver *);
extern int unregister_sparcaudio_driver(struct sparcaudio_driver *);
extern void sparcaudio_output_done(struct sparcaudio_driver *);
extern void sparcaudio_input_done(struct sparcaudio_driver *);
extern int sparcaudio_init(void);
extern int amd7930_init(void);
extern int cs4231_init(void);

#endif

#endif
