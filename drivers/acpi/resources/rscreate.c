/******************************************************************************
 *
 * Module Name: rscreate - Acpi_rs_create_resource_list
 *                         Acpi_rs_create_pci_routing_table
 *                         Acpi_rs_create_byte_stream
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
#include "resource.h"

#define _COMPONENT          RESOURCE_MANAGER
	 MODULE_NAME         ("rscreate");


/***************************************************************************
 * FUNCTION:    Acpi_rs_create_resource_list
 *
 * PARAMETERS:
 *              Byte_stream_buffer      - Pointer to the resource byte stream
 *              Output_buffer           - Pointer to the user's buffer
 *              Output_buffer_length    - Pointer to the size of Output_buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid ACPI_STATUS code
 *                      If Output_buffer is not large enough, Output_buffer_length
 *                        indicates how large Output_buffer should be, else it
 *                        indicates how may u8 elements of Output_buffer are
 *                        valid.
 *
 * DESCRIPTION: Takes the byte stream returned from a _CRS, _PRS control method
 *                  execution and parses the stream to create a linked list
 *                  of device resources.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_rs_create_resource_list (
	ACPI_OBJECT_INTERNAL    *byte_stream_buffer,
	u8                      *output_buffer,
	u32                     *output_buffer_length)
{

	ACPI_STATUS             status = AE_UNKNOWN_STATUS;
	u8                      *byte_stream_start = NULL;
	u32                     list_size_needed = 0;
	u32                     byte_stream_buffer_length = 0;


	/*
	 * Validate parameters:
	 *
	 *  1. If Byte_stream_buffer is NULL after we know that
	 *      Byte_steam_length is not zero, or
	 *  2. If Output_buffer is NULL and Output_buffer_length
	 *      is not zero
	 *
	 *  Return an error
	 */
	if (!byte_stream_buffer ||
	   (!output_buffer && 0 != *output_buffer_length))
	{
		return (AE_BAD_PARAMETER);
	}

	byte_stream_buffer_length = byte_stream_buffer->buffer.length;
	byte_stream_start = byte_stream_buffer->buffer.pointer;

	/*
	 * Pass the Byte_stream_buffer into a module that can calculate
	 *  the buffer size needed for the linked list
	 */
	status = acpi_rs_calculate_list_length (byte_stream_start,
			 byte_stream_buffer_length,
			 &list_size_needed);

	/*
	 * Exit with the error passed back
	 */
	if (AE_OK != status) {
		return (status);
	}

	/*
	 * If the linked list will fit into the available buffer
	 *  call to fill in the list
	 */

	if (list_size_needed <= *output_buffer_length) {
		/*
		 * Zero out the return buffer before proceeding
		 */
		MEMSET (output_buffer, 0x00, *output_buffer_length);

		status = acpi_rs_byte_stream_to_list (byte_stream_start,
				 byte_stream_buffer_length,
				 &output_buffer);

		/*
		 * Exit with the error passed back
		 */
		if (AE_OK != status) {
			return (status);
		}

	}

	else {
		*output_buffer_length = list_size_needed;
		return (AE_BUFFER_OVERFLOW);
	}

	*output_buffer_length = list_size_needed;
	return (AE_OK);

}


/***************************************************************************
 * FUNCTION:    Acpi_rs_create_pci_routing_table
 *
 * PARAMETERS:
 *              Package_object          - Pointer to an ACPI_OBJECT_INTERNAL
 *                                          package
 *              Output_buffer           - Pointer to the user's buffer
 *              Output_buffer_length    - Size of Output_buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid ACPI_STATUS code.
 *              If the Output_buffer is too small, the error will be
 *                AE_BUFFER_OVERFLOW and Output_buffer_length will point
 *                to the size buffer needed.
 *
 * DESCRIPTION: Takes the ACPI_OBJECT_INTERNAL package and creates a
 *                  linked list of PCI interrupt descriptions
 *
 ***************************************************************************/

ACPI_STATUS
acpi_rs_create_pci_routing_table (
	ACPI_OBJECT_INTERNAL    *package_object,
	u8                      *output_buffer,
	u32                     *output_buffer_length)
{
	u8                      *buffer = output_buffer;
	ACPI_OBJECT_INTERNAL    **top_object_list = NULL;
	ACPI_OBJECT_INTERNAL    **sub_object_list = NULL;
	ACPI_OBJECT_INTERNAL    *package_element = NULL;
	u32                     buffer_size_needed = 0;
	u32                     number_of_elements = 0;
	u32                     index = 0;
	u8                      table_index = 0;
	u8                      name_found = FALSE;
	PCI_ROUTING_TABLE       *user_prt = NULL;


	/*
	 * Validate parameters:
	 *
	 *  1. If Method_return_object is NULL, or
	 *  2. If Output_buffer is NULL and Output_buffer_length is not zero
	 *
	 *  Return an error
	 */
	if (!package_object ||
	   (!output_buffer && 0 != *output_buffer_length))
	{
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Calculate the buffer size needed for the routing table.
	 */
	number_of_elements = package_object->package.count;

	/*
	 * Properly calculate the size of the return buffer.
	 *  The base size is the number of elements * the sizes of the
	 *  structures.  Additional space for the strings is added below.
	 *  The minus one is to subtract the size of the u8 Source[1]
	 *  member because it is added below.
	 * NOTE: The Number_of_elements is incremented by one to add an end
	 *  table structure that is essentially a structure of zeros.
	 */
	buffer_size_needed = (number_of_elements + 1) *
			  (sizeof (PCI_ROUTING_TABLE) - 1);

	/*
	 * But each PRT_ENTRY structure has a pointer to a string and
	 *  the size of that string must be found.
	 */
	top_object_list = package_object->package.elements;

	for (index = 0; index < number_of_elements; index++) {
		/*
		 * Dereference the sub-package
		 */
		package_element = *top_object_list;

		/*
		 * The Sub_object_list will now point to an array of the
		 *  four IRQ elements: Address, Pin, Source and Source_index
		 */
		sub_object_list = package_element->package.elements;

		/*
		 * Scan the Irq_table_elements for the Source Name String
		 */
		name_found = FALSE;

		for (table_index = 0; table_index < 4 && !name_found; table_index++) {
			if (ACPI_TYPE_STRING == (*sub_object_list)->common.type) {
				name_found = TRUE;
			}

			else {
				/*
				 * Look at the next element
				 */
				sub_object_list++;
			}
		}

		/*
		 * Was a String type found?
		 */
		if (TRUE == name_found) {
			/*
			 * The length String.Length field includes the
			 *  terminating NULL
			 */
			buffer_size_needed += ((*sub_object_list)->string.length);
		}

		else {
			/*
			 * If no name was found, then this is a NULL, which is
			 *  translated as a u32 zero.
			 */
			buffer_size_needed += sizeof(u32);
		}

		/*
		 * Point to the next ACPI_OBJECT_INTERNAL
		 */
		top_object_list++;
	}

	/*
	 * If the data will fit into the available buffer
	 *  call to fill in the list
	 */
	if (buffer_size_needed <= *output_buffer_length) {
		/*
		 * Zero out the return buffer before proceeding
		 */
		MEMSET (output_buffer, 0x00, *output_buffer_length);

		/*
		 * Loop through the ACPI_INTERNAL_OBJECTS - Each object should
		 *  contain a u32 Address, a u8 Pin, a Name and a u8
		 *  Source_index.
		 */
		top_object_list = package_object->package.elements;

		number_of_elements = package_object->package.count;

		user_prt = (PCI_ROUTING_TABLE *)buffer;

		for (index = 0; index < number_of_elements; index++) {
			/*
			 * Point User_prt past this current structure
			 *
			 * NOTE: On the first iteration, User_prt->Length will
			 *  be zero because we zero'ed out the return buffer
			 *  earlier
			 */
			buffer += user_prt->length;

			user_prt = (PCI_ROUTING_TABLE *)buffer;

			/*
			 * Fill in the Length field with the information we
			 *  have at this point.
			 *  The minus one is to subtract the size of the
			 *  u8 Source[1] member because it is added below.
			 */
			user_prt->length = (sizeof(PCI_ROUTING_TABLE) - 1);

			/*
			 * Dereference the sub-package
			 */
			package_element = *top_object_list;

			/*
			 * The Sub_object_list will now point to an array of
			 *  the four IRQ elements: Address, Pin, Source and
			 *  Source_index
			 */
			sub_object_list = package_element->package.elements;

			/*
			 * Dereference the Address
			 */
			if (ACPI_TYPE_NUMBER == (*sub_object_list)->common.type) {
				user_prt->data.address =
						(*sub_object_list)->number.value;
			}

			else {
				return (AE_BAD_DATA);
			}

			/*
			 * Dereference the Pin
			 */
			sub_object_list++;

			if (ACPI_TYPE_NUMBER == (*sub_object_list)->common.type) {
				user_prt->data.pin =
						(*sub_object_list)->number.value;
			}

			else {
				return (AE_BAD_DATA);
			}

			/*
			 * Dereference the Source Name
			 */
			sub_object_list++;

			if (ACPI_TYPE_STRING == (*sub_object_list)->common.type) {
				STRCPY(user_prt->data.source,
					  (*sub_object_list)->string.pointer);

				/*
				 * Add to the Length field the length of the string
				 */
				user_prt->length += (*sub_object_list)->string.length;
			}

			else {
				/*
				 * If this is a number, then the Source Name
				 *  is NULL, since the entire buffer was zeroed
				 *  out, we can leave this alone.
				 */
				if (ACPI_TYPE_NUMBER == (*sub_object_list)->common.type) {
					/*
					 * Add to the Length field the length of
					 *  the u32 NULL
					 */
					user_prt->length += sizeof(u32);
				}

				else {
					return (AE_BAD_DATA);
				}
			}

			/*
			 * Dereference the Source Index
			 */
			sub_object_list++;

			if (ACPI_TYPE_NUMBER == (*sub_object_list)->common.type) {
				user_prt->data.source_index =
						(*sub_object_list)->number.value;
			}

			else {
				return (AE_BAD_DATA);
			}

			/*
			 * Point to the next ACPI_OBJECT_INTERNAL
			 */
			top_object_list++;
		}

	}

	else {
		*output_buffer_length = buffer_size_needed;

		return (AE_BUFFER_OVERFLOW);
	}

	/*
	 * Report the amount of buffer used
	 */
	*output_buffer_length = buffer_size_needed;

	return (AE_OK);

}


/***************************************************************************
 * FUNCTION:    Acpi_rs_create_byte_stream
 *
 * PARAMETERS:
 *              Linked_list_buffer      - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's buffer
 *              Output_buffer_length    - Size of Output_buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid ACPI_STATUS code.
 *              If the Output_buffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and Output_buffer_length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Takes the linked list of device resources and
 *              creates a bytestream to be used as input for the
 *              _SRS control method.
 *
 ***************************************************************************/

ACPI_STATUS
acpi_rs_create_byte_stream (
	RESOURCE                *linked_list_buffer,
	u8                      *output_buffer,
	u32                     *output_buffer_length)
{
	ACPI_STATUS             status = AE_UNKNOWN_STATUS;
	u32                     byte_stream_size_needed = 0;


	/*
	 * Validate parameters:
	 *
	 *  1. If Linked_list_buffer is NULL, or
	 *  2. If Output_buffer is NULL and Output_buffer_length is not zero
	 *
	 *  Return an error
	 */
	if (!linked_list_buffer ||
	   (!output_buffer && 0 != *output_buffer_length))
	{
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Pass the Linked_list_buffer into a module that can calculate
	 *  the buffer size needed for the byte stream.
	 */
	status = acpi_rs_calculate_byte_stream_length (linked_list_buffer,
			 &byte_stream_size_needed);

	/*
	 * Exit with the error passed back
	 */
	if (AE_OK != status) {
		return (status);
	}

	/*
	 * If the linked list will fit into the available buffer
	 *  call to fill in the list
	 */

	if (byte_stream_size_needed <= *output_buffer_length) {
		/*
		 * Zero out the return buffer before proceeding
		 */
		MEMSET (output_buffer, 0x00, *output_buffer_length);

		status = acpi_rs_list_to_byte_stream (linked_list_buffer,
				 byte_stream_size_needed,
				 &output_buffer);

		/*
		 * Exit with the error passed back
		 */
		if (AE_OK != status) {
			return (status);
		}

	}
	else {
		*output_buffer_length = byte_stream_size_needed;
		return (AE_BUFFER_OVERFLOW);
	}

	return (AE_OK);

}

