/*
 * Exported procedures for the "control" display driver on PowerMacs.
 *
 * Copyright (C) 1997 Paul Mackerras.
 *	
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

extern void map_control_display(struct device_node *);
extern void control_init(void);
extern int control_setmode(struct vc_mode *mode, int doit);
extern void control_set_palette(unsigned char red[], unsigned char green[],
				unsigned char blue[], int index, int ncolors);
extern void control_set_blanking(int blank_mode);
