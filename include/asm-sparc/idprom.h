/* idprom.h: Macros and defines for idprom routines
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

extern struct linux_romvec *romvec;

#define IDPROM_ADDR  (0xffd04000 + 0x7d8)
#define IDPROM_SIZE  36

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

