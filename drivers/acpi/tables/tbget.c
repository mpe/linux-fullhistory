/******************************************************************************
 *
 * Module Name: tbget - ACPI Table get* routines
 *              $Revision: 22 $
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
#include "achware.h"
#include "actables.h"


#define _COMPONENT          TABLE_MANAGER
	 MODULE_NAME         ("tbget")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_ptr
 *
 * PARAMETERS:  Table_type      - one of the defined table types
 *              Instance        - Which table of this type
 *              Table_ptr_loc   - pointer to location to place the pointer for
 *                                return
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the pointer to an ACPI table.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_tb_get_table_ptr (
	ACPI_TABLE_TYPE         table_type,
	u32                     instance,
	ACPI_TABLE_HEADER       **table_ptr_loc)
{
	ACPI_TABLE_DESC         *table_desc;
	u32                     i;


	if (!acpi_gbl_DSDT) {
		return (AE_NO_ACPI_TABLES);
	}

	if (table_type > ACPI_TABLE_MAX) {
		return (AE_BAD_PARAMETER);
	}


	/*
	 * For all table types (Single/Multiple), the first
	 * instance is always in the list head.
	 */

	if (instance == 1) {
		/*
		 * Just pluck the pointer out of the global table!
		 * Will be null if no table is present
		 */

		*table_ptr_loc = acpi_gbl_acpi_tables[table_type].pointer;
		return (AE_OK);
	}


	/*
	 * Check for instance out of range
	 */
	if (instance > acpi_gbl_acpi_tables[table_type].count) {
		return (AE_NOT_EXIST);
	}

	/* Walk the list to get the desired table
	 *  Since the if (Instance == 1) check above checked for the
	 *  first table, setting Table_desc equal to the .Next member
	 *  is actually pointing to the second table.  Therefore, we
	 *  need to walk from the 2nd table until we reach the Instance
	 *  that the user is looking for and return its table pointer.
	 */
	table_desc = acpi_gbl_acpi_tables[table_type].next;
	for (i = 2; i < instance; i++) {
		table_desc = table_desc->next;
	}

	/* We are now pointing to the requested table's descriptor */

	*table_ptr_loc = table_desc->pointer;

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table
 *
 * PARAMETERS:  Physical_address        - Physical address of table to retrieve
 *              *Buffer_ptr             - If Buffer_ptr is valid, read data from
 *                                         buffer rather than searching memory
 *              *Table_info             - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Maps the physical address of table into a logical address
 *
 ******************************************************************************/

ACPI_STATUS
acpi_tb_get_table (
	void                    *physical_address,
	ACPI_TABLE_HEADER       *buffer_ptr,
	ACPI_TABLE_DESC         *table_info)
{
	ACPI_TABLE_HEADER       *table_header = NULL;
	ACPI_TABLE_HEADER       *full_table = NULL;
	u32                     size;
	u8                      allocation;
	ACPI_STATUS             status = AE_OK;


	if (!table_info) {
		return (AE_BAD_PARAMETER);
	}


	if (buffer_ptr) {
		/*
		 * Getting data from a buffer, not BIOS tables
		 */

		table_header = buffer_ptr;
		status = acpi_tb_validate_table_header (table_header);
		if (ACPI_FAILURE (status)) {
			/* Table failed verification, map all errors to BAD_DATA */

			return (AE_BAD_DATA);
		}

		/* Allocate buffer for the entire table */

		full_table = acpi_cm_allocate (table_header->length);
		if (!full_table) {
			return (AE_NO_MEMORY);
		}

		/* Copy the entire table (including header) to the local buffer */

		size = table_header->length;
		MEMCPY (full_table, buffer_ptr, size);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
	}


	/*
	 * Not reading from a buffer, just map the table's physical memory
	 * into our address space.
	 */
	else {
		size = SIZE_IN_HEADER;

		status = acpi_tb_map_acpi_table (physical_address, &size,
				  (void **) &full_table);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
	}


	/* Return values */

	table_info->pointer     = full_table;
	table_info->length      = size;
	table_info->allocation  = allocation;
	table_info->base_pointer = full_table;

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_all_tables
 *
 * PARAMETERS:  Number_of_tables    - Number of tables to get
 *              Table_ptr           - Input buffer pointer, optional
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate all tables other than the RSDT.  The RSDT must
 *              already be loaded and validated.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_tb_get_all_tables (
	u32                     number_of_tables,
	ACPI_TABLE_HEADER       *table_ptr)
{
	ACPI_STATUS             status = AE_OK;
	u32                     index;
	ACPI_TABLE_DESC         table_info;


	/*
	 * Loop through all table pointers found in RSDT.
	 * This will NOT include the FACS and DSDT - we must get
	 * them after the loop
	 */

	for (index = 0; index < number_of_tables; index++) {
		/* Clear the Table_info each time */

		MEMSET (&table_info, 0, sizeof (ACPI_TABLE_DESC));

		/* Get the table via the RSDT */

		status = acpi_tb_get_table ((void *) acpi_gbl_RSDT->table_offset_entry[index],
				 table_ptr, &table_info);

		/* Ignore a table that failed verification */

		if (status == AE_BAD_DATA) {
			continue;
		}

		/* However, abort on serious errors */

		if (ACPI_FAILURE (status)) {
			return (status);
		}

		/* Recognize and install the table */

		status = acpi_tb_install_table (table_ptr, &table_info);
		if (ACPI_FAILURE (status)) {
			/*
			 * Unrecognized or unsupported table, delete it and ignore the
			 * error.  Just get as many tables as we can, later we will
			 * determine if there are enough tables to continue.
			 */

			acpi_tb_delete_single_table (&table_info);
		}
	}


	/*
	 * Get the minimum set of ACPI tables, namely:
	 *
	 * 1) FACP (via RSDT in loop above)
	 * 2) FACS
	 * 3) DSDT
	 *
	 */


	/*
	 * Get the FACS (must have the FACP first, from loop above)
	 * Acpi_tb_get_table_facs will fail if FACP pointer is not valid
	 */

	status = acpi_tb_get_table_facs (table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Install the FACS */

	status = acpi_tb_install_table (table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}


	/* Get the DSDT (We know that the FACP if valid now) */

	status = acpi_tb_get_table ((void *) acpi_gbl_FACP->dsdt, table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Install the DSDT */

	status = acpi_tb_install_table (table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Dump the DSDT Header */

	/* Dump the entire DSDT */

	/*
	 * Initialize the capabilities flags.
	 * Assumes that platform supports ACPI_MODE since we have tables!
	 */

	acpi_gbl_system_flags |= acpi_hw_get_mode_capabilities ();

	return (status);
}

