
/******************************************************************************
 *
 * Name: hardware.h -- hardware specific interfaces
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

#ifndef __HARDWARE_H__
#define __HARDWARE_H__


/* Prototypes */


ACPI_STATUS
acpi_hw_initialize(
	void);

ACPI_STATUS
acpi_hw_shutdown(
	void);

ACPI_STATUS
acpi_hw_initialize_system_info(
	void);

ACPI_STATUS
acpi_hw_set_mode (
	u32                     mode);

u32
acpi_hw_get_mode (
	void);

u32
acpi_hw_get_mode_capabilities (
	void);

/* Register I/O Prototypes */

u32
acpi_hw_register_access (
	NATIVE_UINT             read_write,
	u8                      use_lock,
	u32                     register_id, ... /* DWORD Value */);

void
acpi_hw_clear_acpi_status (
   void);


/* GPE support */

void
acpi_hw_enable_gpe (
	u32                     gpe_index);

void
acpi_hw_disable_gpe (
	u32                     gpe_index);

void
acpi_hw_clear_gpe (
	u32                     gpe_index);

void
acpi_hw_get_gpe_status (
	u32                     gpe_number,
	ACPI_EVENT_STATUS       *event_status);

/* Sleep Prototypes */

ACPI_STATUS
acpi_hw_obtain_sleep_type_register_data (
	u8                      sleep_state,
	u8                      *slp_typ_a,
	u8                      *slp_typ_b);


/* Cx State Prototypes */

ACPI_STATUS
acpi_hw_enter_c1(
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks);

ACPI_STATUS
acpi_hw_enter_c2(
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks);

ACPI_STATUS
acpi_hw_enter_c3(
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks);

ACPI_STATUS
acpi_hw_enter_cx (
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks);

ACPI_STATUS
acpi_hw_set_cx (
	u32                     cx_state);

ACPI_STATUS
acpi_hw_get_cx_info (
	u32                     cx_states[]);


/* Throttling Prototypes */

void
acpi_hw_enable_throttling (
	ACPI_IO_ADDRESS         pblk_address);

void
acpi_hw_disable_throttling (
	ACPI_IO_ADDRESS         pblk_address);

u32
acpi_hw_get_duty_cycle (
	u8                      duty_offset,
	ACPI_IO_ADDRESS         pblk_address,
	u32                     num_throttle_states);

void
acpi_hw_program_duty_cycle (
	u8                      duty_offset,
	u32                     duty_cycle,
	ACPI_IO_ADDRESS         pblk_address,
	u32                     num_throttle_states);

NATIVE_UINT
acpi_hw_local_pow (
	NATIVE_UINT             x,
	NATIVE_UINT             y);


/* ACPI Timer prototypes */

u32
acpi_hw_pmt_ticks (
	void);

u32
acpi_hw_pmt_resolution (
	void);


#endif /* __HARDWARE_H__ */
