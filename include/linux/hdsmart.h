#ifndef _LINUX_HDSMART_H
#define _LINUX_HDSMART_H

/*
 * This file contains some defines for the AT-hd-controller.
 * Various sources.  
 */

#define NR_ATTRIBUTES	30

typedef struct threshold_s {
	unsigned char	id;
	unsigned char	threshold;
	unsigned char	reserved[10];
} __attribute__ ((packed)) threshold_t;

typedef struct thresholds_s {
	unsigned short	revision;
	threshold_t	thresholds[NR_ATTRIBUTES];
	unsigned char	reserved[18];
	unsigned char	vendor[131];
	unsigned char	checksum;
} __attribute__ ((packed)) thresholds_t;

typedef struct value_s {
	unsigned char	id;
	unsigned short	status;
	unsigned char	value;
	unsigned char	vendor[8];
} __attribute__ ((packed)) value_t;

typedef struct values_s {
	unsigned short	revision;
	value_t		values[NR_ATTRIBUTES];
	unsigned char	offline_status;
	unsigned char	vendor1;
	unsigned short	offline_timeout;
	unsigned char	vendor2;
	unsigned char	offline_capability;
	unsigned short	smart_capability;
	unsigned char	reserved[16];
	unsigned char	vendor[125];
	unsigned char	checksum;
} __attribute__ ((packed)) values_t;

#if !defined(__KERNEL__) || defined(_IDE_DISK_C)
 
#define NR_OFFLINE_TEXTS	5
struct {
	unsigned char	value;
	char		*text;
} offline_status_text[NR_OFFLINE_TEXTS] = {
	{ 0x00, "NeverStarted" },
	{ 0x02, "Completed" },
	{ 0x04, "Suspended" },
	{ 0x05, "Aborted" },
	{ 0x06, "Failed" }
};
#endif /* !defined(__KERNEL__) || defined(_IDE_DISK_C) */

#endif	/* _LINUX_HDSMART_H */
