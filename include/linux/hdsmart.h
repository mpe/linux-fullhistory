/*
 * linux/include/linux/hdsmart.h
 *
 * Copyright (C) 1999-2000	Michael Cornwell <cornwell@acm.org>
 * Copyright (C) 2000		Andre Hedrick <andre@linux-ide.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_HDSMART_H
#define _LINUX_HDSMART_H

/* smart_attribute is the vendor specific in SFF-8035 spec */
struct ata_smart_attribute {
	unsigned char				id;
	unsigned short				status_flag;
	unsigned char				normalized;
	unsigned char				worse_normal;
	unsigned char				raw[6];
	unsigned char				reserv;
} __attribute__ ((packed));

/* smart_values is format of the read drive Atrribute command */
struct ata_smart_values {
	unsigned short				revnumber;
	struct ata_smart_attribute		vendor_attributes [30];
        unsigned char				offline_data_collection_status;
        unsigned char				self_test_exec_status;
	unsigned short				total_time_to_complete_off_line;
	unsigned char				vendor_specific_366;
	unsigned char				offline_data_collection_capability;
	unsigned short				smart_capability;
	unsigned char				errorlog_capability;
	unsigned char				vendor_specific_371;
	unsigned char				short_test_completion_time;
	unsigned char				extend_test_completion_time;
	unsigned char				reserved_374_385 [12];
	unsigned char				vendor_specific_386_509 [125];
	unsigned char				chksum;
} __attribute__ ((packed));

/* Smart Threshold data structures */
/* Vendor attribute of SMART Threshold */
struct ata_smart_threshold_entry {
	unsigned char				id;
	unsigned char				normalized_threshold;
	unsigned char				reserved[10];
} __attribute__ ((packed));

/* Format of Read SMART THreshold Command */
struct ata_smart_thresholds {
	unsigned short				revnumber;
	struct ata_smart_threshold_entry	thres_entries[30];
	unsigned char				reserved[149];
	unsigned char				chksum;
} __attribute__ ((packed));

struct ata_smart_errorlog_command_struct {
	unsigned char				devicecontrolreg;
	unsigned char				featuresreg;
	unsigned char				sector_count;
	unsigned char				sector_number;
	unsigned char				cylinder_low;
	unsigned char				cylinder_high;
	unsigned char				drive_head;
	unsigned char				commandreg;
	unsigned int				timestamp;
} __attribute__ ((packed));

struct ata_smart_errorlog_error_struct {
	unsigned char				error_condition;
	unsigned char				extended_error[14];
	unsigned char				state;
	unsigned short				timestamp;
} __attribute__ ((packed));

struct ata_smart_errorlog_struct {
	struct ata_smart_errorlog_command_struct	commands[6];
	struct ata_smart_errorlog_error_struct		error_struct;
}  __attribute__ ((packed));

struct ata_smart_errorlog {
	unsigned char				revnumber;
	unsigned char				error_log_pointer;
	struct ata_smart_errorlog_struct	errorlog_struct[5];
	unsigned short				ata_error_count;
	unsigned short				non_fatal_count;
	unsigned short				drive_timeout_count;
	unsigned char				reserved[53];
} __attribute__ ((packed));

struct ata_smart_selftestlog_struct {
	unsigned char				selftestnumber;
	unsigned char				selfteststatus;
	unsigned short				timestamp;
	unsigned char				selftestfailurecheckpoint;
	unsigned int				lbafirstfailure;
	unsigned char				vendorspecific[15];
} __attribute__ ((packed));

struct ata_smart_selftestlog {
	unsigned short				revnumber;
	struct ata_smart_selftestlog_struct	selftest_struct[21];
	unsigned char				vendorspecific[2];
	unsigned char				mostrecenttest;
	unsigned char				resevered[2];
	unsigned char				chksum;
} __attribute__ ((packed));

#if !defined(__KERNEL__) || defined(_IDE_DISK_C)
/* smartctl version number */
#define VERSION_MAJOR           		1
#define VERSION_MINOR           		2

/* Number of ata device to scan */
int numdevices;

/* how often SMART is checks in seconds */
int checktime = 1800;

typedef struct atadevices_s {
	int					fd;
	char					devicename[14];
	int					selftest;
	struct hd_driveid			drive;
	struct ata_smart_values			smartval;
	struct ata_smart_thresholds		smartthres;
} atadevices_t;

#endif /* !defined(__KERNEL__) || defined(_IDE_DISK_C) */

#endif	/* _LINUX_HDSMART_H */
