/* Driver for SCM Microsystems USB-ATAPI cable
 * Header File
 *
 * Current development and maintainance by:
 *   (c) 2000 Robert Baruch (autophile@dol.net)
 *
 * See scm.c for more explanation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _USB_SCM_H
#define _USB_SCM_H

#define SCM_EPP_PORT		0x10
#define SCM_EPP_REGISTER	0x30
#define SCM_ATA			0x40
#define SCM_ISA			0x50

/* SCM User I/O Data registers */

#define SCM_UIO_EPAD		0x80 // Enable Peripheral Control Signals
#define SCM_UIO_CDT		0x40 // Card Detect (Read Only)
				     // CDT = ACKD & !UI1 & !UI0
#define SCM_UIO_1		0x20 // I/O 1
#define SCM_UIO_0		0x10 // I/O 0
#define SCM_UIO_EPP_ATA		0x08 // 1=EPP mode, 0=ATA mode
#define SCM_UIO_UI1		0x04 // Input 1
#define SCM_UIO_UI0		0x02 // Input 0
#define SCM_UIO_INTR_ACK	0x01 // Interrupt (ATA & ISA)/Acknowledge (EPP)

/* SCM User I/O Enable registers */

#define SCM_UIO_DRVRST		0x80 // Reset Peripheral
#define SCM_UIO_ACKD		0x40 // Enable Card Detect
#define SCM_UIO_OE1		0x20 // I/O 1 set=output/clr=input
				     // If ACKD=1, set OE1 to 1 also.
#define SCM_UIO_OE0		0x10 // I/O 0 set=output/clr=input
#define SCM_UIO_ADPRST		0x01 // Reset SCM chip

/* SCM-specific commands */

extern int scm_read(struct us_data *us, unsigned char access,
	unsigned char reg, unsigned char *content);
extern int scm_write(struct us_data *us, unsigned char access,
	unsigned char reg, unsigned char content);
extern int scm_read_block(struct us_data *us, unsigned char access,
	unsigned char reg, unsigned char *content, unsigned short len,
	int use_sg);
extern int scm_write_block(struct us_data *us, unsigned char access,
	unsigned char reg, unsigned char *content, unsigned short len,
	int use_sg);
extern int scm_multiple_write(struct us_data *us, unsigned char access,
	unsigned char *registers, unsigned char *data_out,
	unsigned short num_registers);
extern int scm_read_user_io(struct us_data *us, unsigned char *data_flags);
extern int scm_write_user_io(struct us_data *us,
	unsigned char enable_flags, unsigned char data_flags);

/* HP 8200e stuff */

extern int hp8200e_transport(Scsi_Cmnd *srb, struct us_data *us);


#endif
