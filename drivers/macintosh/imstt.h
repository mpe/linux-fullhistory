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

extern void map_imstt_display_ibm(struct device_node *);
extern void map_imstt_display_tvp(struct device_node *);
extern void imstt_init(void);
extern int imstt_setmode(struct vc_mode *mode, int doit);
extern void imstt_set_palette_ibm(unsigned char red[], unsigned char green[],
				unsigned char blue[], int index, int ncolors);
extern void imstt_set_palette_tvp(unsigned char red[], unsigned char green[],
				unsigned char blue[], int index, int ncolors);
extern void imstt_set_blanking(int blank_mode);

