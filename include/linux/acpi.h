/*
 *  acpi.h - ACPI driver interface
 *
 *  Copyright (C) 1999 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* /dev/acpi minor number */
#define ACPI_MINOR_DEV 167

/* RSDP location */
#define ACPI_BIOS_ROM_BASE ((__u8*) 0xe0000)
#define ACPI_BIOS_ROM_END  ((__u8*) 0x100000)

/* Table signatures */
#define ACPI_RSDP1_SIG 0x20445352 /* 'RSD ' */
#define ACPI_RSDP2_SIG 0x20525450 /* 'PTR ' */
#define ACPI_RSDT_SIG  0x54445352 /* 'RSDT' */
#define ACPI_FACP_SIG  0x50434146 /* 'FACP' */
#define ACPI_DSDT_SIG  0x54445344 /* 'DSDT' */

/* PM1_STS flags */
#define ACPI_TMR_STS    0x0001
#define ACPI_BM_STS     0x0010
#define ACPI_GBL_STS    0x0020
#define ACPI_PWRBTN_STS 0x0100
#define ACPI_SLPBTN_STS 0x0200
#define ACPI_RTC_STS    0x0400
#define ACPI_WAK_STS    0x8000

/* PM1_EN flags */
#define ACPI_TMR_EN    0x0001
#define ACPI_GBL_EN    0x0020
#define ACPI_PWRBTN_EN 0x0100
#define ACPI_SLPBTN_EN 0x0200
#define ACPI_RTC_EN    0x0400

/* PM1_CNT flags */
#define ACPI_SCI_EN   0x0001
#define ACPI_BM_RLD   0x0002
#define ACPI_GBL_RLS  0x0004
#define ACPI_SLP_TYP0 0x0400
#define ACPI_SLP_TYP1 0x0800
#define ACPI_SLP_TYP2 0x1000
#define ACPI_SLP_EN   0x2000

/* PM_TMR masks */
#define ACPI_TMR_MASK   0x00ffffff
#define ACPI_TMR_HZ	3580000 /* 3.58 MHz */

/* strangess to avoid integer overflow */
#define ACPI_uS_TO_TMR_TICKS(val) \
  (((val) * (ACPI_TMR_HZ / 10000)) / 100)

/* PM2_CNT flags */
#define ACPI_ARB_DIS 0x01

/* FACP flags */
#define ACPI_WBINVD       0x00000001
#define ACPI_WBINVD_FLUSH 0x00000002
#define ACPI_PROC_C1      0x00000004
#define ACPI_P_LVL2_UP    0x00000008
#define ACPI_PWR_BUTTON   0x00000010
#define ACPI_SLP_BUTTON   0x00000020
#define ACPI_FIX_RTC      0x00000040
#define ACPI_RTC_64       0x00000080
#define ACPI_TMR_VAL_EXT  0x00000100
#define ACPI_DCK_CAP      0x00000200

struct acpi_rsdp {
	__u32 signature[2];
	__u8 checksum;
	__u8 oem[6];
	__u8 reserved;
	__u32 rsdt;
};

struct acpi_table {
	__u32 signature;
	__u32 length;
	__u8 rev;
	__u8 checksum;
	__u8 oem[6];
	__u8 oem_table[8];
	__u32 oem_rev;
	__u32 creator;
	__u32 creator_rev;
};

struct acpi_facp {
	struct acpi_table hdr;
	__u32 facs;
	__u32 dsdt;
	__u8 int_model;
	__u8 reserved;
	__u16 sci_int;
	__u32 smi_cmd;
	__u8 acpi_enable;
	__u8 acpi_disable;
	__u8 s4bios_req;
	__u8 reserved2;
	__u32 pm1a_evt;
	__u32 pm1b_evt;
	__u32 pm1a_cnt;
	__u32 pm1b_cnt;
	__u32 pm2_cnt;
	__u32 pm_tmr;
	__u32 gpe0;
	__u32 gpe1;
	__u8 pm1_evt_len;
	__u8 pm1_cnt_len;
	__u8 pm2_cnt_len;
	__u8 pm_tm_len;
	__u8 gpe0_len;
	__u8 gpe1_len;
	__u8 gpe1_base;
	__u8 reserved3;
	__u16 p_lvl2_lat;
	__u16 p_lvl3_lat;
	__u16 flush_size;
	__u16 flush_stride;
	__u8 duty_offset;
	__u8 duty_width;
	__u8 day_alarm;
	__u8 mon_alarm;
	__u8 century;
	__u8 reserved4;
	__u8 reserved5;
	__u8 reserved6;
	__u32 flags;
};

#define ACPI_FIND_TABLES	_IOR('A', 1, struct acpi_find_tables)
#define ACPI_WAIT_EVENT		_IO('A', 2)

struct acpi_find_tables {
	unsigned long facp;
	unsigned long dsdt;
};

#ifdef __KERNEL__

extern void (*acpi_idle)(void);

#endif

#endif /* _LINUX_ACPI_H */
