/*
 * linux/include/asm-arm/arch-nexuspci/time.h
 *
 * Copyright (c) 1997 Phil Blundell.
 *
 * Nexus PCI card has no real-time clock.
 *
 */

extern __inline__ unsigned long gettimeoffset (void)
{
	return 0;
}

extern __inline__ int reset_timer (void)
{
	return 0;
}

extern __inline__ unsigned long setup_timer (void)
{
	reset_timer ();
	/*
	 * Default the date to 1 Jan 1970 0:0:0
	 * You will have to run a time daemon to set the
	 * clock correctly at bootup
	 */
	return mktime(1970, 1, 1, 0, 0, 0);
}
