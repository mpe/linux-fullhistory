/*
 * sysctl.h - Header file for sysctl.c
 *
 * Copyright (C) 1997 Martin von L�wis
 * Copyright (C) 1997 R�gis Duchesne
 */

#ifdef DEBUG
	extern int ntdebug;

	void ntfs_sysctl(int add);

	#define SYSCTL(x)	ntfs_sysctl(x)
#else
	#define SYSCTL(x)
#endif /* DEBUG */

