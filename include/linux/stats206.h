/* stats206.h. Define constants to gather statistics on the cm206 behavior.
   Copyright (c) 1995 David van Leeuwen.
   
   This is published under the Gnu Public Licence, read the header in 
   the file cm206.c.
*/

/* This is an ugly way to guarantee that the names of the statistics
 * are the same in the code and in the diagnostics program.  */

#ifdef __KERNEL__
#define x(a) st_ ## a
#define y enum
#else
#define x(a) #a
#define y char * stats_name[] = 
#endif

y {x(interrupt), x(data_ready), x(fifo_overflow), x(data_error),
     x(crc_error), x(sync_error), x(lost_intr), x(echo),
     x(write_timeout), x(receive_timeout), x(read_timeout),
     x(dsb_timeout), x(stop_0xff), x(back_read_timeout),
     x(sector_transferred), x(read_restarted), x(read_background),
     x(bh), x(open), x(ioctl_multisession)
#ifdef __KERNEL__
     , x(last_entry)
#endif
 };

#ifdef __KERNEL__
#define NR_STATS st_last_entry
#else
#define NR_STATS sizeof(stats_name)/sizeof(char*)
#endif
