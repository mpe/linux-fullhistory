
/******************************************************************************
 *
 * Name: events.h - Acpi_event subcomponent prototypes and defines
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

#ifndef __EVENTS_H__
#define __EVENTS_H__


/*
 * Acpi_evfixed - Fixed event handling
 */

ACPI_STATUS
acpi_ev_fixed_event_initialize (
	void);

u32
acpi_ev_fixed_event_detect (
	void);

u32
acpi_ev_fixed_event_dispatch (
	u32                     acpi_event);


/*
 * Acpi_evglock - Global Lock support
 */

ACPI_STATUS
acpi_ev_acquire_global_lock(
	void);

void
acpi_ev_release_global_lock(
	void);

ACPI_STATUS
acpi_ev_init_global_lock_handler (
	void);


/*
 * Acpi_evgpe - GPE handling and dispatch
 */

ACPI_STATUS
acpi_ev_gpe_initialize (
	void);

ACPI_STATUS
acpi_ev_init_gpe_control_methods (
	void);

u32
acpi_ev_gpe_dispatch (
	u32                     gpe_number);

u32
acpi_ev_gpe_detect (
	void);


/*
 * Acpi_evnotify - Device Notify handling and dispatch
 */

void
acpi_ev_notify_dispatch (
	ACPI_HANDLE             device,
	u32                     notify_value);


/*
 * Acpi_evregion - Address Space handling
 */

ACPI_STATUS
acpi_ev_install_default_address_space_handlers (
	void);

ACPI_STATUS
acpi_ev_address_space_dispatch (
	ACPI_OBJECT_INTERNAL   *region_obj,
	u32                     function,
	u32                     address,
	u32                     bit_width,
	u32                     *value);


ACPI_STATUS
acpi_ev_addr_handler_helper (
	ACPI_HANDLE             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value);

void
acpi_ev_disassociate_region_from_handler(
	ACPI_OBJECT_INTERNAL   *region_obj);


ACPI_STATUS
acpi_ev_associate_region_and_handler(
	ACPI_OBJECT_INTERNAL    *handler_obj,
	ACPI_OBJECT_INTERNAL    *region_obj);


/*
 * Acpi_evregini - Region initialization and setup
 */

ACPI_STATUS
acpi_ev_system_memory_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context);

ACPI_STATUS
acpi_ev_io_space_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context);

ACPI_STATUS
acpi_ev_pci_config_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context);

ACPI_STATUS
acpi_ev_default_region_setup (
	ACPI_HANDLE             handle,
	u32                     function,
	void                    *handler_context,
	void                    **return_context);

ACPI_STATUS
acpi_ev_initialize_region (
	ACPI_OBJECT_INTERNAL    *region_obj,
	u8                      acpi_ns_locked);


/*
 * Acpi_evsci - SCI (System Control Interrupt) handling/dispatch
 */

u32
acpi_ev_install_sci_handler (
	void);

ACPI_STATUS
acpi_ev_remove_sci_handler (
	void);

s32
acpi_ev_initialize_sCI (
	s32                     program_sCI);

void
acpi_ev_restore_acpi_state (
	void);

void
acpi_ev_terminate (
	void);


/* Debug support */

#ifdef ACPI_DEBUG

s32
acpi_ev_sci_count (
	u32                     acpi_event);

#define DEBUG_INCREMENT_EVENT_COUNT(a)   acpi_gbl_event_count[a]++;

#else

#define DEBUG_INCREMENT_EVENT_COUNT(a)
#endif


#endif  /*  __EVENTS_H__   */
