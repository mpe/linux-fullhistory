/* $Id: idprom.h,v 1.5 1995/11/25 02:31:49 davem Exp $
 * idprom.h: Macros and defines for idprom routines
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_IDPROM_H
#define _SPARC_IDPROM_H

extern struct linux_romvec *romvec;

/* Offset into the EEPROM where the id PROM is located on the 4c */
#define IDPROM_OFFSET  0x7d8

/* On sun4m; physical. */
/* MicroSPARC(-II) does not decode 31rd bit, but it works. */
#define IDPROM_OFFSET_M  0xfd8

struct idp_struct
{
  unsigned char id_f_id;      /* format identifier */
  unsigned char id_machtype;  /* Machine type */
  unsigned char id_eaddr[6];  /* hardware ethernet address */
  long id_domf;               /* Date when this machine was manufactured */
  unsigned int id_sernum:24;  /* Unique serial number */
  unsigned char id_cksum;     /* XXX */
  unsigned char dummy[16];    /* XXX */
};

extern struct idp_struct *idprom;

#define IDPROM_SIZE  (sizeof(struct idp_struct))

#endif /* !(_SPARC_IDPROM_H) */
