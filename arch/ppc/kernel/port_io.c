/*
 * I/O 'port' access routines
 */

/* This is really only correct for the MVME16xx (PreP)? */

#define _IO_BASE ((unsigned long)0x80000000)

unsigned char
inb(int port)
{
	return (*((unsigned  char *)(_IO_BASE+port)));
}

unsigned short
inw(int port)
{
	return (_LE_to_BE_short(*((unsigned short *)(_IO_BASE+port))));
}

unsigned long
inl(int port)
{
	return (_LE_to_BE_long(*((unsigned  long *)(_IO_BASE+port))));
}

void insl(int port, long *ptr, int len)
{
	unsigned long *io_ptr = (unsigned long *)(_IO_BASE+port);
	while (len-- > 0)
	{
		*ptr++ = _LE_to_BE_long(*io_ptr);
	}
}

unsigned char  inb_p(int port) {return (inb(port)); }
unsigned short inw_p(int port) {return (inw(port)); }
unsigned long  inl_p(int port) {return (inl(port)); }

unsigned char
outb(unsigned char val,int port)
{
	*((unsigned  char *)(_IO_BASE+port)) = (val);
	return (val);
}

unsigned short
outw(unsigned short val,int port)
{
	*((unsigned  short *)(_IO_BASE+port)) = _LE_to_BE_short(val);
	return (val);
}

unsigned long
outl(unsigned long val,int port)
{
	*((unsigned  long *)(_IO_BASE+port)) = _LE_to_BE_long(val);
	return (val);
}

void outsl(int port, long *ptr, int len)
{
	unsigned long *io_ptr = (unsigned long *)(_IO_BASE+port);
	while (len-- > 0)
	{
		*io_ptr = _LE_to_BE_long(*ptr++);
	}
}

unsigned char  outb_p(unsigned char val,int port) { return (outb(val,port)); }
unsigned short outw_p(unsigned short val,int port) { return (outw(val,port)); }
unsigned long  outl_p(unsigned long val,int port) { return (outl(val,port)); }

