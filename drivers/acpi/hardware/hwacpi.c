
/******************************************************************************
 *
 * Module Name: hwacpi - ACPI hardware functions - mode and timer
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


#include "acpi.h"
#include "hardware.h"


#define _COMPONENT          HARDWARE
	 MODULE_NAME         ("hwacpi");


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_set_mode
 *
 * PARAMETERS:  Mode            - SYS_MODE_ACPI or SYS_MODE_LEGACY
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transitions the system into the requested mode or does nothing
 *              if the system is already in that mode.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_hw_set_mode (
	u32                     mode)
{

	ACPI_STATUS             status = AE_ERROR;


	if (mode == SYS_MODE_ACPI) {
		/* BIOS should have disabled ALL fixed and GP events */

		acpi_os_out8 (acpi_gbl_FACP->smi_cmd, acpi_gbl_FACP->acpi_enable);
	}

	else if (mode == SYS_MODE_LEGACY) {
		/*
		 * BIOS should clear all fixed status bits and restore fixed event
		 * enable bits to default
		 */

		acpi_os_out8 (acpi_gbl_FACP->smi_cmd, acpi_gbl_FACP->acpi_disable);
	}

	if (acpi_hw_get_mode () == mode) {
		status = AE_OK;
	}

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw

 *
 * PARAMETERS:  none
 *
 * RETURN:      SYS_MODE_ACPI or SYS_MODE_LEGACY
 *
 * DESCRIPTION: Return current operating state of system.  Determined by
 *              querying the SCI_EN bit.
 *
 ******************************************************************************/

u32
acpi_hw_get_mode (void)
{


	if (acpi_hw_register_access (ACPI_READ, ACPI_MTX_LOCK, (s32)SCI_EN)) {
		return (SYS_MODE_ACPI);
	}
	else {
		return (SYS_MODE_LEGACY);
	}
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_mode_capabilities
 *
 * PARAMETERS:  none
 *
 * RETURN:      logical OR of SYS_MODE_ACPI and SYS_MODE_LEGACY determined at initial
 *              system state.
 *
 * DESCRIPTION: Returns capablities of system
 *
 ******************************************************************************/

u32
acpi_hw_get_mode_capabilities (void)
{


	if (!(acpi_gbl_system_flags & SYS_MODES_MASK)) {
		if (acpi_hw_get_mode () == SYS_MODE_LEGACY) {
			/*
			 * Assume that if this call is being made, Acpi_init has been called
			 * and ACPI support has been established by the presence of the
			 * tables.  Therefore since we're in SYS_MODE_LEGACY, the system
			 * must support both modes
			 */

			acpi_gbl_system_flags |= (SYS_MODE_ACPI | SYS_MODE_LEGACY);
		}

		else {
			/* TBD: [Investigate] !!! this may be unsafe... */
			/*
			 * system is is ACPI mode, so try to switch back to LEGACY to see if
			 * it is supported
			 */
			acpi_hw_set_mode (SYS_MODE_LEGACY);

			if (acpi_hw_get_mode () == SYS_MODE_LEGACY) {
				/* Now in SYS_MODE_LEGACY, so both are supported */

				acpi_gbl_system_flags |= (SYS_MODE_ACPI | SYS_MODE_LEGACY);
				acpi_hw_set_mode (SYS_MODE_ACPI);
			}

			else {
				/* Still in SYS_MODE_ACPI so this must be an ACPI only system */

				acpi_gbl_system_flags |= SYS_MODE_ACPI;
			}
		}
	}

	return (acpi_gbl_system_flags & SYS_MODES_MASK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_pmt_ticks
 *
 * PARAMETERS:  none
 *
 * RETURN:      Current value of the ACPI PMT (timer)
 *
 * DESCRIPTION: Obtains current value of ACPI PMT
 *
 ******************************************************************************/

u32
acpi_hw_pmt_ticks (void)
{
	u32                      ticks;

	ticks = acpi_os_in32 (acpi_gbl_FACP->pm_tmr_blk);

	return (ticks);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_pmt_resolution
 *
 * PARAMETERS:  none
 *
 * RETURN:      Number of bits of resolution in the PMT (either 24 or 32)
 *
 * DESCRIPTION: Obtains resolution of the ACPI PMT (either 24bit or 32bit)
 *
 ******************************************************************************/

u32
acpi_hw_pmt_resolution (void)
{
	if (0 == acpi_gbl_FACP->tmr_val_ext) {
		return (24);
	}

	return (32);
}

