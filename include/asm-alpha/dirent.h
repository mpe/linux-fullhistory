#ifndef _ALPHA_DIRENT_H
#define _ALPHA_DIRENT_H

struct dirent {
	ino_t		d_ino;
	unsigned short	d_reclen;
	unsigned short	d_namlen;
	char		d_name[256];
};

#endif
