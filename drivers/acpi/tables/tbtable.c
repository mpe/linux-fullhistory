/******************************************************************************
 *
 * Module Name: tbtable - ACPI tables: FACP, FACS, and RSDP utilities
 *              $Revision: 24 $
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
	 MODULE_NAME         ("tbtable")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_rsdt
 *
 * PARAMETERS:  Number_of_tables    - Where the table count is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate the RSDP (ptr) and RSDT (table)
 *
 ******************************************************************************/

ACPI_STATUS
acpi_tb_get_table_rsdt (
	u32                     *number_of_tables)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_TABLE_DESC         table_info;


	/* Get the RSDP */

	status = acpi_tb_find_rsdp (&table_info);
	if (ACPI_FAILURE (status)) {
		REPORT_WARNING ("RSDP structure not found");
		return (AE_NO_ACPI_TABLES);
	}

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor (ACPI_TABLE_RSDP, &table_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	acpi_gbl_RSDP = (ROOT_SYSTEM_DESCRIPTOR_POINTER *) table_info.pointer;


	/*
	 * RSDP structure was found;  Now get the RSDT
	 */

	status = acpi_tb_get_table ((void *) acpi_gbl_RSDP->rsdt_physical_address, NULL,
			 &table_info);
	if (ACPI_FAILURE (status)) {
		if (status == AE_BAD_SIGNATURE) {
			/* Invalid RSDT signature */

			REPORT_ERROR ("Invalid signature where RSDP indicates RSDT should be located");

		}
		REPORT_ERROR ("Unable to locate RSDT");

		return (status);
	}


	/* Always delete the RSDP mapping */

	acpi_tb_delete_acpi_table (ACPI_TABLE_RSDP);

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor (ACPI_TABLE_RSDT, &table_info);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	acpi_gbl_RSDT = (ROOT_SYSTEM_DESCRIPTION_TABLE *) table_info.pointer;


	/* Valid RSDT signature, verify the checksum */

	status = acpi_tb_verify_table_checksum ((ACPI_TABLE_HEADER *) acpi_gbl_RSDT);

	/*
	 * Determine the number of tables pointed to by the RSDT.
	 * This is defined by the ACPI Specification to be the number of
	 * pointers contained within the RSDT.  The size of the pointers
	 * is architecture-dependent.
	 */

	*number_of_tables = ((acpi_gbl_RSDT->header.length -
			   sizeof (ACPI_TABLE_HEADER)) / sizeof (void *));


	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_scan_memory_for_rsdp
 *
 * PARAMETERS:  Start_address       - Starting pointer for search
 *              Length              - Maximum length to search
 *
 * RETURN:      Pointer to the RSDP if found, otherwise NULL.
 *
 * DESCRIPTION: Search a block of memory for the RSDP signature
 *
 ******************************************************************************/

u8 *
acpi_tb_scan_memory_for_rsdp (
	u8                      *start_address,
	u32                     length)
{
	u32                     offset;
	u8                      *mem_rover;


	/* Search from given start addr for the requested length  */

	for (offset = 0, mem_rover = start_address;
		 offset < length;
		 offset += RSDP_SCAN_STEP, mem_rover += RSDP_SCAN_STEP)
	{

		/* The signature and checksum must both be correct */

		if (STRNCMP ((NATIVE_CHAR *) mem_rover, RSDP_SIG, sizeof (RSDP_SIG)-1) == 0 &&
			acpi_tb_checksum (mem_rover,
				sizeof (ROOT_SYSTEM_DESCRIPTOR_POINTER)) == 0)
		{
			/* If so, we have found the RSDP */

			return (mem_rover);
		}
	}

	/* Searched entire block, no RSDP was found */

	return (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_find_rsdp
 *
 * PARAMETERS:  *Buffer_ptr             - If == NULL, read data from buffer
 *                                        rather than searching memory
 *              *Table_info             - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search lower 1_mbyte of memory for the root system descriptor
 *              pointer structure.  If it is found, set *RSDP to point to it.
 *
 *              NOTE: The RSDP must be either in the first 1_k of the Extended
 *              BIOS Data Area or between E0000 and FFFFF (ACPI 1.0 section
 *              5.2.2; assertion #421).
 *
 ******************************************************************************/

ACPI_STATUS
acpi_tb_find_rsdp (
	ACPI_TABLE_DESC         *table_info)
{
	u8                      *table_ptr;
	u8                      *mem_rover;
	ACPI_STATUS             status = AE_OK;

	if (acpi_gbl_acpi_init_data.RSDP_physical_address) {
		/*
		 *  RSDP address was supplied as part of the initialization data
		 */

		status = acpi_os_map_memory(acpi_gbl_acpi_init_data.RSDP_physical_address,
				 sizeof (ROOT_SYSTEM_DESCRIPTOR_POINTER),
				 (void **)&table_ptr);

		if (ACPI_FAILURE (status)) {
			return (status);
		}

		if (!table_ptr) {
			return (AE_NO_MEMORY);
		}

		/*
		 *  The signature and checksum must both be correct
		 */

		if (STRNCMP ((NATIVE_CHAR *) table_ptr, RSDP_SIG, sizeof (RSDP_SIG)-1) != 0) {
			/* Nope, BAD Signature */
			acpi_os_unmap_memory (table_ptr, sizeof (ROOT_SYSTEM_DESCRIPTOR_POINTER));
			return (AE_BAD_SIGNATURE);
		}

		/* The signature and checksum must both be correct */

		if (acpi_tb_checksum (table_ptr,
				sizeof (ROOT_SYSTEM_DESCRIPTOR_POINTER)) != 0)
		{
			/* Nope, BAD Checksum */
			acpi_os_unmap_memory (table_ptr, sizeof (ROOT_SYSTEM_DESCRIPTOR_POINTER));
			return (AE_BAD_CHECKSUM);
		}

		/* RSDP supplied is OK */
		/* If so, we have found the RSDP */

		table_info->pointer     = (ACPI_TABLE_HEADER *) table_ptr;
		table_info->length      = sizeof (ROOT_SYSTEM_DESCRIPTOR_POINTER);
		table_info->allocation  = ACPI_MEM_MAPPED;
		table_info->base_pointer = table_ptr;

		return (AE_OK);
	}

	/*
	 * Search memory for RSDP.  First map low physical memory.
	 */

	status = acpi_os_map_memory (LO_RSDP_WINDOW_BASE, LO_RSDP_WINDOW_SIZE,
			  (void **)&table_ptr);

	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/*
	 * 1) Search EBDA (low memory) paragraphs
	 */

	if (NULL != (mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr,
			  LO_RSDP_WINDOW_SIZE)))
	{
		/* Found it, return pointer and don't delete the mapping */

		table_info->pointer     = (ACPI_TABLE_HEADER *) mem_rover;
		table_info->length      = LO_RSDP_WINDOW_SIZE;
		table_info->allocation  = ACPI_MEM_MAPPED;
		table_info->base_pointer = table_ptr;

		return (AE_OK);
	}

	/* This mapping is no longer needed */

	acpi_os_unmap_memory (table_ptr, LO_RSDP_WINDOW_SIZE);


	/*
	 * 2) Search upper memory: 16-byte boundaries in E0000h-F0000h
	 */

	status = acpi_os_map_memory (HI_RSDP_WINDOW_BASE, HI_RSDP_WINDOW_SIZE,
			  (void **)&table_ptr);

	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (NULL != (mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr,
			  HI_RSDP_WINDOW_SIZE)))
	{
		/* Found it, return pointer and don't delete the mapping */

		table_info->pointer     = (ACPI_TABLE_HEADER *) mem_rover;
		table_info->length      = HI_RSDP_WINDOW_SIZE;
		table_info->allocation  = ACPI_MEM_MAPPED;
		table_info->base_pointer = table_ptr;

		return (AE_OK);
	}

	/* This mapping is no longer needed */

	acpi_os_unmap_memory (table_ptr, HI_RSDP_WINDOW_SIZE);


	/* RSDP signature was not found */

	return (AE_NOT_FOUND);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_facs
 *
 * PARAMETERS:  *Buffer_ptr             - If Buffer_ptr is valid, read data from
 *                                          buffer rather than searching memory
 *              *Table_info             - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns a pointer to the FACS as defined in FACP.  This
 *              function assumes the global variable FACP has been
 *              correctly initialized.  The value of FACP->Firmware_ctrl
 *              into a far pointer which is returned.
 *
 *****************************************************************************/

ACPI_STATUS
acpi_tb_get_table_facs (
	ACPI_TABLE_HEADER       *buffer_ptr,
	ACPI_TABLE_DESC         *table_info)
{
	void                    *table_ptr = NULL;
	u32                     size;
	u8                      allocation;
	ACPI_STATUS             status = AE_OK;


	/* Must have a valid FACP pointer */

	if (!acpi_gbl_FACP) {
		return (AE_NO_ACPI_TABLES);
	}

	size = sizeof (FIRMWARE_ACPI_CONTROL_STRUCTURE);
	if (buffer_ptr) {
		/*
		 * Getting table from a file -- allocate a buffer and
		 * read the table.
		 */
		table_ptr = acpi_cm_allocate (size);
		if(!table_ptr) {
			return (AE_NO_MEMORY);
		}

		MEMCPY (table_ptr, buffer_ptr, size);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
	}

	else {
		/* Just map the physical memory to our address space */

		status = acpi_tb_map_acpi_table ((void *) acpi_gbl_FACP->firmware_ctrl,
				   &size, &table_ptr);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
	}


	/* Return values */

	table_info->pointer     = table_ptr;
	table_info->length      = size;
	table_info->allocation  = allocation;
	table_info->base_pointer = table_ptr;

	return (status);
}

