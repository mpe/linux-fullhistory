/* $Id: file.c,v 1.1 1997/06/06 09:37:03 ralf Exp $
 * file.c: ARCS firmware interface to files.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <asm/sgialib.h>

long prom_getvdirent(unsigned long fd, struct linux_vdirent *ent, unsigned long num,
		     unsigned long *cnt)
{
	return romvec->get_vdirent(fd, ent, num, cnt);
}

long prom_open(char *name, enum linux_omode md, unsigned long *fd)
{
	return romvec->open(name, md, fd);
}

long prom_close(unsigned long fd)
{
	return romvec->close(fd);
}

long prom_read(unsigned long fd, void *buf, unsigned long num, unsigned long *cnt)
{
	return romvec->read(fd, buf, num, cnt);
}

long prom_getrstatus(unsigned long fd)
{
	return romvec->get_rstatus(fd);
}

long prom_write(unsigned long fd, void *buf, unsigned long num, unsigned long *cnt)
{
	return romvec->write(fd, buf, num, cnt);
}

long prom_seek(unsigned long fd, struct linux_bigint *off, enum linux_seekmode sm)
{
	return romvec->seek(fd, off, sm);
}

long prom_mount(char *name, enum linux_mountops op)
{
	return romvec->mount(name, op);
}

long prom_getfinfo(unsigned long fd, struct linux_finfo *buf)
{
	return romvec->get_finfo(fd, buf);
}

long prom_setfinfo(unsigned long fd, unsigned long flags, unsigned long msk)
{
	return romvec->set_finfo(fd, flags, msk);
}
