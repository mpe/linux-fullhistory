
/******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
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
#include "namesp.h"

#define _COMPONENT          HARDWARE
	 MODULE_NAME         ("hwregs");


/* This matches the #defines in actypes.h. */

ACPI_STRING sleep_state_table[] = {"\\_S0_","\\_S1_","\\_S2_","\\_S3_",
			   "\\_S4_","\\_S4_b","\\_S5_"};


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_bit_shift
 *
 * PARAMETERS:  Mask            - Input mask to determine bit shift from.
 *                                Must have at least 1 bit set.
 *
 * RETURN:      Bit location of the lsb of the mask
 *
 * DESCRIPTION: Returns the bit number for the low order bit that's set.
 *
 ******************************************************************************/

s32
acpi_hw_get_bit_shift (
	u32                     mask)
{
	s32                     shift;


	for (shift = 0; ((mask >> shift) & 1) == 0; shift++) { ; }

	return (shift);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *
 ******************************************************************************/

void
acpi_hw_clear_acpi_status (void)
{
	u16                     gpe_length;
	u16                     index;


	acpi_cm_acquire_mutex (ACPI_MTX_HARDWARE);

	acpi_os_out16 (acpi_gbl_FACP->pm1a_evt_blk, (u16) ALL_FIXED_STS_BITS);

	if (acpi_gbl_FACP->pm1b_evt_blk) {
		acpi_os_out16 ((u16) acpi_gbl_FACP->pm1b_evt_blk,
				  (u16) ALL_FIXED_STS_BITS);
	}

	/* now clear the GPE Bits */

	if (acpi_gbl_FACP->gpe0blk_len) {
		gpe_length = (u16) DIV_2 (acpi_gbl_FACP->gpe0blk_len);

		for (index = 0; index < gpe_length; index++) {
			acpi_os_out8 ((acpi_gbl_FACP->gpe0blk + index), (u8) 0xff);
		}
	}

	if (acpi_gbl_FACP->gpe1_blk_len) {
		gpe_length = (u16) DIV_2 (acpi_gbl_FACP->gpe1_blk_len);

		for (index = 0; index < gpe_length; index++) {
			acpi_os_out8 ((acpi_gbl_FACP->gpe1_blk + index), (u8) 0xff);
		}
	}

	acpi_cm_release_mutex (ACPI_MTX_HARDWARE);
	return;
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_obtain_sleep_type_register_data
 *
 * PARAMETERS:  Sleep_state       - Numeric state requested
 *              *Slp_Typ_a         - Pointer to byte to receive SLP_TYPa value
 *              *Slp_Typ_b         - Pointer to byte to receive SLP_TYPb value
 *
 * RETURN:      Status - ACPI status
 *
 * DESCRIPTION: Acpi_hw_obtain_sleep_type_register_data() obtains the SLP_TYP and
 *              SLP_TYPb values for the sleep state requested.
 *

 ***************************************************************************/

ACPI_STATUS
acpi_hw_obtain_sleep_type_register_data (
	u8                      sleep_state,
	u8                      *slp_typ_a,
	u8                      *slp_typ_b)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_OBJECT_INTERNAL    *obj_desc;


	/*
	 *  Validate parameters
	 */

	if ((sleep_state > ACPI_S_STATES_MAX) ||
		!slp_typ_a || !slp_typ_b)
	{
		return (AE_BAD_PARAMETER);
	}

	/*
	 *  Acpi_evaluate the namespace object containing the values for this state
	 */

	status = acpi_ns_evaluate_by_name (sleep_state_table[sleep_state], NULL, &obj_desc);
	if (AE_OK == status) {
		if (obj_desc) {
			/*
			 *  We got something, now ensure it is correct.  The object must
			 *  be a package and must have at least 2 numeric values as the
			 *  two elements
			 */

			if ((obj_desc->common.type != ACPI_TYPE_PACKAGE) ||
				((obj_desc->package.elements[0])->common.type !=
					ACPI_TYPE_NUMBER) ||
				((obj_desc->package.elements[1])->common.type !=
					ACPI_TYPE_NUMBER))
			{
				/* Invalid _Sx_ package type or value  */

				REPORT_ERROR ("Object type returned from interpreter differs from expected value");
				status = AE_ERROR;
			}
			else {
				/*
				 *  Valid _Sx_ package size, type, and value
				 */
				*slp_typ_a =
					(u8) (obj_desc->package.elements[0])->number.value;

				*slp_typ_b =
					(u8) (obj_desc->package.elements[1])->number.value;
			}

			acpi_cm_remove_reference (obj_desc);
		}
	}

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_register_access
 *
 * PARAMETERS:  Read_write      - Either ACPI_READ or ACPI_WRITE.
 *              Use_lock        - Lock the hardware
 *              Register_id     - index of ACPI register to access
 *              Value           - (only used on write) value to write to the
 *                                 register.  Shifted all the way right.
 *
 * RETURN:      Value written to or read from specified register.  This value
 *              is shifted all the way right.
 *
 * DESCRIPTION: Generic ACPI register read/write function.
 *
 ******************************************************************************/

u32
acpi_hw_register_access (
	NATIVE_UINT             read_write,
	u8                      use_lock,
	u32                     register_id,
	...)                    /* Value (only used on write) */
{
	u32                     register_value = 0;
	u32                     mask = 0;
	u32                     value = 0;
	ACPI_IO_ADDRESS         gpe_reg = 0;


	if (read_write == ACPI_WRITE) {
		va_list         marker;

		va_start (marker, register_id);
		value = va_arg (marker, s32);
		va_end (marker);
	}

	/*
	 * TBD: [Restructure] May want to split the Acpi_event code and the
	 * Control code
	 */

	/*
	 * Decode the Register ID
	 */

	switch (register_id & REGISTER_BLOCK_MASK)
	{
	case PM1_EVT:

		if (register_id < TMR_EN) {
			/* status register */

			if (ACPI_MTX_LOCK == use_lock) {
				acpi_cm_acquire_mutex (ACPI_MTX_HARDWARE);
			}


			register_value = (u32) acpi_os_in16 (acpi_gbl_FACP->pm1a_evt_blk);
			if (acpi_gbl_FACP->pm1b_evt_blk) {
				register_value |= (u32) acpi_os_in16 (acpi_gbl_FACP->pm1b_evt_blk);
			}

			switch (register_id)
			{
			case TMR_STS:
				mask = TMR_STS_MASK;
				break;

			case BM_STS:
				mask = BM_STS_MASK;
				break;

			case GBL_STS:
				mask = GBL_STS_MASK;
				break;

			case PWRBTN_STS:
				mask = PWRBTN_STS_MASK;
				break;

			case SLPBTN_STS:
				mask = SLPBTN_STS_MASK;
				break;

			case RTC_STS:
				mask = RTC_STS_MASK;
				break;

			case WAK_STS:
				mask = WAK_STS_MASK;
				break;

			default:
				mask = 0;
				break;
			}

			if (read_write == ACPI_WRITE) {
				/*
				 * Status registers are different from the rest.  Clear by
				 * writing 1, writing 0 has no effect.  So, the only relevent
				 * information is the single bit we're interested in, all
				 * others should be written as 0 so they will be left
				 * unchanged
				 */

				value <<= acpi_hw_get_bit_shift (mask);
				value &= mask;

				if (value) {
					acpi_os_out16 (acpi_gbl_FACP->pm1a_evt_blk, (u16) value);

					if (acpi_gbl_FACP->pm1b_evt_blk) {
						acpi_os_out16 (acpi_gbl_FACP->pm1b_evt_blk, (u16) value);
					}

					register_value = 0;
				}
			}

			if (ACPI_MTX_LOCK == use_lock) {
				acpi_cm_release_mutex (ACPI_MTX_HARDWARE);
			}
		}

		else {
			/* enable register */

			if (ACPI_MTX_LOCK == use_lock) {
				acpi_cm_acquire_mutex (ACPI_MTX_HARDWARE);
			}

			register_value = (u32) acpi_os_in16 (acpi_gbl_FACP->pm1a_evt_blk +
					  DIV_2 (acpi_gbl_FACP->pm1_evt_len));

			if (acpi_gbl_FACP->pm1b_evt_blk) {
				register_value |= (u32) acpi_os_in16 (acpi_gbl_FACP->pm1b_evt_blk +
						   DIV_2 (acpi_gbl_FACP->pm1_evt_len));

			}

			switch (register_id)
			{
			case TMR_EN:
				mask = TMR_EN_MASK;
				break;

			case GBL_EN:
				mask = GBL_EN_MASK;
				break;

			case PWRBTN_EN:
				mask = PWRBTN_EN_MASK;
				break;

			case SLPBTN_EN:
				mask = SLPBTN_EN_MASK;
				break;

			case RTC_EN:
				mask = RTC_EN_MASK;
				break;

			default:
				mask = 0;
				break;
			}

			if (read_write == ACPI_WRITE) {
				register_value &= ~mask;
				value          <<= acpi_hw_get_bit_shift (mask);
				value          &= mask;
				register_value |= value;

				acpi_os_out16 ((acpi_gbl_FACP->pm1a_evt_blk +
						 DIV_2 (acpi_gbl_FACP->pm1_evt_len)),
						 (u16) register_value);

				if (acpi_gbl_FACP->pm1b_evt_blk) {
					acpi_os_out16 ((acpi_gbl_FACP->pm1b_evt_blk +
							 DIV_2 (acpi_gbl_FACP->pm1_evt_len)),
							 (u16) register_value);
				}
			}
			if(ACPI_MTX_LOCK == use_lock) {
				acpi_cm_release_mutex (ACPI_MTX_HARDWARE);
			}
		}
		break;


	case PM1_CONTROL:

		register_value = 0;

		if (ACPI_MTX_LOCK == use_lock) {
			acpi_cm_acquire_mutex (ACPI_MTX_HARDWARE);
		}

		if (register_id != SLP_TYPE_B) {
			/*
			 * SLP_TYPx registers are written differently
			 * than any other control registers with
			 * respect to A and B registers.  The value
			 * for A may be different than the value for B
			 */

			register_value = (u32) acpi_os_in16 (acpi_gbl_FACP->pm1a_cnt_blk);
		}

		if (acpi_gbl_FACP->pm1b_cnt_blk && register_id != (s32) SLP_TYPE_A) {
			register_value |= (u32) acpi_os_in16 (acpi_gbl_FACP->pm1b_cnt_blk);
		}

		switch (register_id)
		{
		case SCI_EN:
			mask = SCI_EN_MASK;
			break;

		case BM_RLD:
			mask = BM_RLD_MASK;
			break;

		case GBL_RLS:
			mask = GBL_RLS_MASK;
			break;

		case SLP_TYPE_A:
		case SLP_TYPE_B:
			mask = SLP_TYPE_X_MASK;
			break;

		case SLP_EN:
			mask = SLP_EN_MASK;
			break;

		default:
			mask = 0;
			break;
		}

		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			/*
			 * SLP_TYPE_x registers are written differently
			 * than any other control registers with
			 * respect to A and B registers.  The value
			 * for A may be different than the value for B
			 */

			if (register_id != SLP_TYPE_B) {
				if (mask == SLP_EN_MASK) {
					disable();  /* disable interrupts */
				}

				acpi_os_out16 (acpi_gbl_FACP->pm1a_cnt_blk, (u16) register_value);

				if (mask == SLP_EN_MASK) {
					/*
					 * Enable interrupts, the SCI handler is likely going to
					 * be invoked as soon as interrupts are enabled, since gpe's
					 * and most fixed resume events also generate SCI's.
					 */
					enable();
				}
			}

			if (acpi_gbl_FACP->pm1b_cnt_blk && register_id != (s32) SLP_TYPE_A) {
				acpi_os_out16 (acpi_gbl_FACP->pm1b_cnt_blk, (u16) register_value);
			}
		}

		if (ACPI_MTX_LOCK == use_lock) {
			acpi_cm_release_mutex (ACPI_MTX_HARDWARE);
		}
		break;


	case PM2_CONTROL:

		if (ACPI_MTX_LOCK == use_lock) {
			acpi_cm_acquire_mutex (ACPI_MTX_HARDWARE);
		}

		register_value = (u32) acpi_os_in16 (acpi_gbl_FACP->pm2_cnt_blk);
		switch (register_id)
		{
		case ARB_DIS:
			mask = ARB_DIS_MASK;
			break;

		default:
			mask = 0;
			break;
		}

		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			acpi_os_out16 (acpi_gbl_FACP->pm2_cnt_blk, (u16) register_value);
		}

		if (ACPI_MTX_LOCK == use_lock) {
			acpi_cm_release_mutex (ACPI_MTX_HARDWARE);
		}
		break;


	case PM_TIMER:

		register_value = acpi_os_in32 (acpi_gbl_FACP->pm_tmr_blk);
		mask = 0xFFFFFFFF;
		break;


	case GPE1_EN_BLOCK:

		gpe_reg = (acpi_gbl_FACP->gpe1_blk + acpi_gbl_FACP->gpe1_base) +
				 (gpe_reg + (DIV_2 (acpi_gbl_FACP->gpe1_blk_len)));


	case GPE1_STS_BLOCK:

		if (!gpe_reg) {
			gpe_reg = (acpi_gbl_FACP->gpe1_blk + acpi_gbl_FACP->gpe1_base);
		}


	case GPE0_EN_BLOCK:

		if (!gpe_reg) {
			gpe_reg = acpi_gbl_FACP->gpe0blk + DIV_2 (acpi_gbl_FACP->gpe0blk_len);
		}


	case GPE0_STS_BLOCK:

		if (!gpe_reg) {
			gpe_reg = acpi_gbl_FACP->gpe0blk;
		}

		/* Determine the bit to be accessed */

		mask = (((u32) register_id) & BIT_IN_REGISTER_MASK);
		mask = 1 << (mask-1);

		/* The base address of the GPE 0 Register Block */
		/* Plus 1/2 the length of the GPE 0 Register Block */
		/* The enable register is the register following the Status Register */
		/* and each register is defined as 1/2 of the total Register Block */

		/* This sets the bit within Enable_bit that needs to be written to */
		/* the register indicated in Mask to a 1, all others are 0 */

		if (mask > LOW_BYTE) {
			/* Shift the value 1 byte to the right and add 1 to the register */

			mask >>= ONE_BYTE;
			gpe_reg++;
		}

		/* Now get the current Enable Bits in the selected Reg */

		if(ACPI_MTX_LOCK == use_lock) {
			acpi_cm_acquire_mutex (ACPI_MTX_HARDWARE);
		}

		register_value = (u32) acpi_os_in8 (gpe_reg);
		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			/* This write will put the Action state into the General Purpose */

			/* Enable Register indexed by the value in Mask */

			acpi_os_out8 (gpe_reg, (u8) register_value);
			register_value = (u32) acpi_os_in8 (gpe_reg);
		}

		if(ACPI_MTX_LOCK == use_lock) {
			acpi_cm_release_mutex (ACPI_MTX_HARDWARE);
		}
		break;


	case PROCESSOR_BLOCK:
	default:

		mask = 0;
		break;
	}


	register_value &= mask;
	register_value >>= acpi_hw_get_bit_shift (mask);

	return (register_value);
}
