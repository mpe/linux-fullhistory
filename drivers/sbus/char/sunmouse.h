/* $Id: sunmouse.h,v 1.1 1997/08/28 02:23:38 ecd Exp $
 * sunmouse.h: Interface to the SUN mouse driver.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC_SUNMOUSE_H
#define _SPARC_SUNMOUSE_H 1

extern void sun_mouse_zsinit(void);
extern void sun_mouse_inbyte(unsigned char);

#endif /* !(_SPARC_SUNMOUSE_H) */
