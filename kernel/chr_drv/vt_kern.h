#ifndef _VT_KERN_H
#define _VT_KERN_H

extern struct vt_info {
	int mode;			/* KD_TEXT, ... */
} vt_info[MAX_CONSOLES];

#endif /* _VT_KERN_H */
