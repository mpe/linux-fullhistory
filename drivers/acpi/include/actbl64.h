
/******************************************************************************
 *
 * Name: actbl64.h - ACPI tables specific to IA64
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
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

#ifndef __ACTBL64_H__
#define __ACTBL64_H__


typedef UINT64              IO_ADDRESS;             /* Only for clarity in declarations */


/* IA64 Root System Description Table */

typedef struct
{
	ACPI_TABLE_HEADER       header;                 /* Table header */
	u32                     reserved_pad;           /* IA64 alignment, must be 0 */
	void                    *table_offset_entry [1]; /* Array of pointers to other */
			 /* tables' headers */
} ROOT_SYSTEM_DESCRIPTION_TABLE;


/* IA64 Firmware ACPI Control Structure */

typedef struct
{
	char                    signature[4];           /* signature "FACS" */
	u32                     length;                 /* length of structure, in bytes */
	u32                     hardware_signature;     /* hardware configuration signature */
	u32                     reserved4;              /* must be 0 */
	UINT64                  firmware_waking_vector; /* ACPI OS waking vector */
	UINT64                  global_lock;            /* Global Lock */
	u32                     S4_bios_f       : 1;    /* Indicates if S4_bIOS support is present */
	u32                     reserved1       : 31;   /* must be 0 */
	u8                      resverved3 [28];        /* reserved - must be zero */

} FIRMWARE_ACPI_CONTROL_STRUCTURE;


/* IA64 Fixed ACPI Description Table */

typedef struct
{
	ACPI_TABLE_HEADER       header;                 /* table header */
	u32                     reserved_pad;           /* IA64 alignment, must be 0 */
	ACPI_TBLPTR             firmware_ctrl;          /* Physical address of FACS */
	ACPI_TBLPTR             acpi_dsdt;                  /* Physical address of DSDT */
	u8                      model;                  /* System Interrupt Model */
	u8                      address_space;          /* Address Space Bitmask */
	u16                     sci_int;                /* System vector of SCI interrupt */
	u8                      acpi_enable;            /* value to write to smi_cmd to enable ACPI */
	u8                      acpi_disable;           /* value to write to smi_cmd to disable ACPI */
	u8                      S4_bios_req;            /* Value to write to SMI CMD to enter S4_bIOS state */
	u8                      reserved2;              /* reserved - must be zero */
	UINT64                  smi_cmd;                /* Port address of SMI command port */
	UINT64                  pm1a_evt_blk;           /* Port address of Power Mgt 1a Acpi_event Reg Blk */
	UINT64                  pm1b_evt_blk;           /* Port address of Power Mgt 1b Acpi_event Reg Blk */
	UINT64                  pm1a_cnt_blk;           /* Port address of Power Mgt 1a Control Reg Blk */
	UINT64                  pm1b_cnt_blk;           /* Port address of Power Mgt 1b Control Reg Blk */
	UINT64                  pm2_cnt_blk;            /* Port address of Power Mgt 2 Control Reg Blk */
	UINT64                  pm_tmr_blk;             /* Port address of Power Mgt Timer Ctrl Reg Blk */
	UINT64                  gpe0blk;                /* Port addr of General Purpose Acpi_event 0 Reg Blk */
	UINT64                  gpe1_blk;               /* Port addr of General Purpose Acpi_event 1 Reg Blk */
	u8                      pm1_evt_len;            /* Byte Length of ports at pm1_x_evt_blk */
	u8                      pm1_cnt_len;            /* Byte Length of ports at pm1_x_cnt_blk */
	u8                      pm2_cnt_len;            /* Byte Length of ports at pm2_cnt_blk */
	u8                      pm_tm_len;              /* Byte Length of ports at pm_tm_blk */
	u8                      gpe0blk_len;            /* Byte Length of ports at gpe0_blk */
	u8                      gpe1_blk_len;           /* Byte Length of ports at gpe1_blk */
	u8                      gpe1_base;              /* offset in gpe model where gpe1 events start */
	u8                      reserved3;              /* reserved */
	u16                     Plvl2_lat;              /* worst case HW latency to enter/exit C2 state */
	u16                     Plvl3_lat;              /* worst case HW latency to enter/exit C3 state */
	u8                      day_alrm;               /* index to day-of-month alarm in RTC CMOS RAM */
	u8                      mon_alrm;               /* index to month-of-year alarm in RTC CMOS RAM */
	u8                      century;                /* index to century in RTC CMOS RAM */
	u8                      reserved4;              /* reserved */
	u32                     flush_cash      : 1;    /* PAL_FLUSH_CACHE is correctly supported */
	u32                     reserved5       : 1;    /* reserved - must be zero */
	u32                     proc_c1         : 1;    /* all processors support C1 state */
	u32                     Plvl2_up        : 1;    /* C2 state works on MP system */
	u32                     pwr_button      : 1;    /* Power button is handled as a generic feature */
	u32                     sleep_button    : 1;    /* Sleep button is handled as a generic feature, or not present */
	u32                     fixed_rTC       : 1;    /* RTC wakeup stat not in fixed register space */
	u32                     RTCS4           : 1;    /* RTC wakeup stat not possible from S4 */
	u32                     tmr_val_ext     : 1;    /* tmr_val is 32 bits */
	u32                     dock_cap        : 1;    /* Supports Docking */
	u32                     reserved6       : 22;    /* reserved - must be zero */

}  FIXED_ACPI_DESCRIPTION_TABLE;


#endif /* __ACTBL64_H__ */

