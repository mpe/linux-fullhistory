/* $Id: envctrl.h,v 1.1 1998/05/16 17:26:07 ecd Exp $
 *
 * envctrl.h: Definitions for access to the i2c environment
 *            monitoring on Ultrasparc systems.
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC64_ENVCTRL_H
#define _SPARC64_ENVCTRL_H 1

#include <linux/ioctl.h>

#define I2CIOCSADR _IOW('p', 0x40, int)
#define I2CIOCGADR _IOR('p', 0x41, int)

#endif /* !(_SPARC64_ENVCTRL_H) */
