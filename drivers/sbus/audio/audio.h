/*
 * drivers/sbus/audio/audio.h
 *
 * Sparc Audio Midlayer
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 */

#ifndef _AUDIO_H_
#define _AUDIO_H_

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/wait.h>


#define NR_SPARCAUDIO_DRIVERS 1


struct sparcaudio_driver
{
	const char * name;
	struct sparcaudio_operations *ops;
	void *private;

	/* Nonzero if the driver is busy. */
	int busy;

	/* Support for a circular queue of output buffers. */
	__u8 **output_buffers;
	size_t *output_sizes;
	int num_output_buffers, output_front, output_rear, output_count, output_active;
	struct wait_queue *output_write_wait, *output_drain_wait;
};

struct sparcaudio_operations
{
	int (*open)(struct inode *, struct file *, struct sparcaudio_driver *);
	void (*release)(struct inode *, struct file *, struct sparcaudio_driver *);
	int (*ioctl)(struct inode *, struct file *, unsigned int, unsigned long,
		     struct sparcaudio_driver *);

	/* Ask driver to begin playing a buffer. */
	void (*start_output)(struct sparcaudio_driver *, __u8 * buffer, size_t count);

	/* Ask driver to stop playing a buffer. */
	void (*stop_output)(struct sparcaudio_driver *);

	/* Set and get the audio encoding method. */
	int (*set_encoding)(int encoding);
	int (*get_encoding)(void);

	/* Set and get the audio sampling rate (samples per second). */
	int (*set_sampling_rate)(int sampling_rate);
	int (*get_sampling_rate)(void);

	/* Set and get the audio output port. */
	int (*set_output_port)(int output_port);
	int (*get_output_port)(void);

	/* Set and get the audio input port. */
	int (*set_input_port)(int input_port);
	int (*get_input_port)(void);
};

extern int register_sparcaudio_driver(struct sparcaudio_driver *);
extern int unregister_sparcaudio_driver(struct sparcaudio_driver *);
extern void sparcaudio_output_done(void);
extern int sparcaudio_init(void);
extern int amd7930_init(void);

#endif

#endif
