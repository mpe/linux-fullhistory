/*
 * Exported procedures for the PowerMac "platinum" display adaptor.
 *
 * Copyright (C) 1996 Paul Mackerras and Mark Abene.
 *	
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

extern void map_platinum(struct device_node *);
extern void platinum_init(void);
extern int platinum_setmode(struct vc_mode *mode, int doit);
extern void platinum_set_palette(unsigned char red[], unsigned char green[],
				 unsigned char blue[], int index, int ncolors);
extern void platinum_set_blanking(int blank_mode);
