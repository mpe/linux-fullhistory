/*
 *  acpi.c - Linux ACPI arch-specific functions
 *
 *  Copyright (C) 1999-2000 Andrew Henroid
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

/*
 * Changes:
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 2000/08/31
 * - check copy*user return
 * - get rid of check_region
 * - get rid of verify_area
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 2000/09/28
 * - do proper release on failure in acpi_claim_ioports and acpi_init
 * Andrew Grover <andrew.grover@intel.com> - 2000/11/13
 * - Took out support for user-level interpreter. ACPI 2.0 changes preclude
 *   its maintenance.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pm.h>

#define _LINUX
#include <linux/acpi.h>
/* Is there a better way to include this? */
#include <../drivers/acpi/include/acpi.h>

ACPI_PHYSICAL_ADDRESS
acpi_get_rsdp_ptr()
{
	ACPI_PHYSICAL_ADDRESS rsdp_phys;

	if(ACPI_SUCCESS(acpi_find_root_pointer(&rsdp_phys)))
		return rsdp_phys;
	else
		return 0;
}
