  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* routines to support remote kgdb to Linux/AP+ cells */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>

static char out_buf[0x100];
static int out_buf_pos = 0;

static char in_buf[0x100];
static int in_buf_pos = 0;
static int in_buf_count = 0;

static int hash_pos = -1;

void ap_dbg_flush(void)
{
	struct cap_request req;
	
	if (out_buf_pos == 0) return;
	
	req.cid = mpp_cid();
	req.type = REQ_PUTDEBUGSTRING;
	req.size = sizeof(req) + out_buf_pos;
	req.header = MAKE_HEADER(HOST_CID);
	
	write_bif_polled((char *)&req,sizeof(req),(char *)out_buf,out_buf_pos);
	
	out_buf_pos = 0;
	hash_pos = -1;
}

/* called by the gdb stuff */
void putDebugChar(char c)
{
	if (c == '#') hash_pos = out_buf_pos;
	
	out_buf[out_buf_pos++] = c;
	if (out_buf_pos == sizeof(out_buf)) {
		ap_dbg_flush();
	}
}

/* used by gdb to get input */
char getDebugChar(void)
{
	unsigned flags;
	struct cap_request req;
	
	ap_dbg_flush();
	
	if (in_buf_count == 0) {
		req.cid = mpp_cid();
		req.type = REQ_GETDEBUGCHAR;
		req.size = sizeof(req);
		req.header = MAKE_HEADER(HOST_CID);
		
		save_flags(flags); cli();
		write_bif_polled((char *)&req,sizeof(req),NULL,0);
		ap_wait_request(&req,REQ_GETDEBUGCHAR);
		read_bif(in_buf,req.size - sizeof(req));
		in_buf_pos = 0;
		in_buf_count = req.size - sizeof(req);
		restore_flags(flags);
	}
	
	in_buf_count--;
	return(in_buf[in_buf_pos++]);
}
