/*
 * Exported procedures for the chips65550 display driver on PowerBook 3400/2400
 *
 * Copyright (C) 1997 Fabio Riccardi.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

extern void map_chips_display(struct device_node *);
extern void chips_init(void);
extern int chips_setmode(struct vc_mode *mode, int doit);
extern void chips_set_palette(unsigned char red[], unsigned char green[],
				 unsigned char blue[], int index, int ncolors);
extern void chips_set_blanking(int blank_mode);
