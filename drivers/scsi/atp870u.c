/* $Id: atp870u.c,v 1.0 1997/05/07 15:22:00 root Exp root $
 *  linux/kernel/atp870u.c
 *
 *  Copyright (C) 1997	Wu Ching Chen
 *  2.1.x update (C) 1998  Krzysztof G. Baranowski
 *
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/pci.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"


#include "atp870u.h"

#include<linux/stat.h>

struct proc_dir_entry proc_scsi_atp870u = {
    PROC_SCSI_ATP870U, 7, "atp870u",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

void mydlyu(unsigned int);
/*
static const char RCSid[] = "$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/atp870u.c,v 1.0 1997/05/07 15:22:00 root Exp root $";
*/

static unsigned char admaxu=1,host_idu[2],chip_veru[2],scam_on[2],global_map[2];
static unsigned short int active_idu[2],wide_idu[2],sync_idu,ultra_map[2];
static int  workingu[2]={0,0};
static Scsi_Cmnd *querequ[2][qcnt],*curr_req[2][16];
static unsigned char devspu[2][16] = {{0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
				0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20},
			       {0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
				0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20}};
static unsigned char dirctu[2][16],last_cmd[2],in_snd[2],in_int[2];
static unsigned char ata_cdbu[2][16];
static unsigned int ioportu[2]={0,0};
static unsigned int irqnumu[2]={0,0};
static unsigned short int pciportu[2];
static unsigned long prdaddru[2][16],tran_lenu[2][16],last_lenu[2][16];
static unsigned char prd_tableu[2][16][1024];
static unsigned char *prd_posu[2][16];
static unsigned char quhdu[2],quendu[2];
static unsigned char devtypeu[2][16] = {{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
				 { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}};
static struct Scsi_Host * atp_host[2]={NULL,NULL};

static void atp870u_intr_handle(int irq, void *dev_id, struct pt_regs *regs)
{
    unsigned short int	tmpcip,id;
    unsigned char	i,j,h,tarid,lun;
    unsigned char  *prd;
    Scsi_Cmnd *workrequ;
    unsigned int workportu,tmport;
    unsigned long adrcntu,k;
    int      errstus;

    for ( h=0; h < 2; h++ )
    {
	if ( ( irq & 0x0f ) == irqnumu[h] )
	{
	   goto irq_numok;
	}
    }
    return;
irq_numok:
    in_int[h]=1;
    workportu=ioportu[h];
    tmport=workportu;

    if ( workingu[h] != 0 )
    {
       tmport += 0x1f;
       j=inb(tmport);
       tmpcip=pciportu[h];
       if ((inb(tmpcip) & 0x08) != 0)
       {
	  tmpcip += 0x2;
	  while((inb(tmpcip) & 0x08) != 0);
       }
       tmpcip=pciportu[h];
       outb(0x00,tmpcip);
       tmport -=0x08;
       i=inb(tmport);
       if ((j & 0x40) == 0)
       {
	  if ((last_cmd[h] & 0x40) == 0)
	  {
	     last_cmd[h]=0xff;
	  }
       }
       else
       {
	  last_cmd[h] |= 0x40;
       }
       tmport -= 0x02;
       tarid=inb(tmport);
       tmport += 0x02;
       if ((tarid & 0x40) != 0)
       {
	  tarid=(tarid & 0x07) | 0x08;
       }
       else
       {
	  tarid &= 0x07;
       }
       if ( i == 0x85 )
       {
	  if (wide_idu[h] != 0)
	  {
	     tmport=workportu+0x1b;
	     j=inb(tmport) & 0x0e;
	     j |= 0x01;
	     outb(j,tmport);
	  }
	  if (((quhdu[h] != quendu[h]) || (last_cmd[h] != 0xff)) &&
	      (in_snd[h] == 0))
	  {
	     send_s870(h);
	  }
	  in_int[h]=0;
	  return;
       }
       if ( i == 0x21 )
       {
	  tmport -= 0x05;
	  adrcntu=0;
	  ((unsigned char *)&adrcntu)[2]=inb(tmport++);
	  ((unsigned char *)&adrcntu)[1]=inb(tmport++);
	  ((unsigned char *)&adrcntu)[0]=inb(tmport);
	  k=last_lenu[h][tarid];
	  k -= adrcntu;
	  tran_lenu[h][tarid]= k;
	  last_lenu[h][tarid]=adrcntu;
	  tmport -= 0x04;
	  outb(0x41,tmport);
	  tmport += 0x08;
	  outb(0x08,tmport);
	  in_int[h]=0;
	  return ;
       }

       if ((i == 0x80) || (i == 0x8f))
       {
	  lun=0;
	  tmport -= 0x07;
	  j=inb(tmport);
	  if ( j == 0x44 )
	  {
	     tmport += 0x0d;
	     lun=inb(tmport) & 0x07;
	  }
	  else
	  {
	     if ( j == 0x41 )
	     {
		tmport += 0x02;
		adrcntu=0;
		((unsigned char *)&adrcntu)[2]=inb(tmport++);
		((unsigned char *)&adrcntu)[1]=inb(tmport++);
		((unsigned char *)&adrcntu)[0]=inb(tmport);
		k=last_lenu[h][tarid];
		k -= adrcntu;
		tran_lenu[h][tarid]= k;
		last_lenu[h][tarid]=adrcntu;
		tmport += 0x04;
		outb(0x08,tmport);
		in_int[h]=0;
		return ;
	     }
	     else
	     {
		outb(0x46,tmport);
		dirctu[h][tarid]=0x00;
		tmport += 0x02;
		outb(0x00,tmport++);
		outb(0x00,tmport++);
		outb(0x00,tmport++);
		tmport+=0x03;
		outb(0x08,tmport);
		in_int[h]=0;
		return;
	     }
	  }
	  tmport=workportu + 0x10;
	  outb(0x45,tmport);
	  tmport += 0x06;
	  tarid=inb(tmport);
	  if ((tarid & 0x10) != 0)
	  {
	     tarid=(tarid & 0x07) | 0x08;
	  }
	  else
	  {
	     tarid &= 0x07;
	  }
	  workrequ=curr_req[h][tarid];
	  tmport=workportu + 0x0f;
	  outb(lun,tmport);
	  tmport += 0x02;
	  outb(devspu[h][tarid],tmport++);
	  adrcntu=tran_lenu[h][tarid];
	  k=last_lenu[h][tarid];
	  outb(((unsigned char *)&k)[2],tmport++);
	  outb(((unsigned char *)&k)[1],tmport++);
	  outb(((unsigned char *)&k)[0],tmport++);
	  j=tarid;
	  if ( tarid > 7 )
	  {
	     j = (j & 0x07) | 0x40;
	  }
	  j |= dirctu[h][tarid];
	  outb(j,tmport++);
	  outb(0x80,tmport);
	  tmport=workportu + 0x1b;
	  j=inb(tmport) & 0x0e;
	  id=1;
	  id=id << tarid;
	  if ((id & wide_idu[h]) != 0)
	  {
	     j |= 0x01;
	  }
	  outb(j,tmport);
	  if ( last_lenu[h][tarid] == 0 )
	  {
	     tmport=workportu + 0x18;
	     outb(0x08,tmport);
	     in_int[h]=0;
	     return ;
	  }
	  prd=prd_posu[h][tarid];
	  while ( adrcntu != 0 )
	  {
	       id=((unsigned short int *)(prd))[2];
	       if ( id == 0 )
	       {
		  k=0x10000;
	       }
	       else
	       {
		  k=id;
	       }
	       if ( k > adrcntu )
	       {
		  ((unsigned short int *)(prd))[2] =(unsigned short int)
						     (k - adrcntu);
		  ((unsigned long *)(prd))[0] += adrcntu;
		  adrcntu=0;
		  prd_posu[h][tarid]=prd;
	       }
	       else
	       {
		  adrcntu -= k;
		  prdaddru[h][tarid] += 0x08;
		  prd += 0x08;
		  if ( adrcntu == 0 )
		  {
		     prd_posu[h][tarid]=prd;
		  }
	       }
	  }
	  tmpcip=pciportu[h] + 0x04;
	  outl(prdaddru[h][tarid],tmpcip);
	  tmpcip -= 0x02;
	  outb(0x06,tmpcip);
	  outb(0x00,tmpcip);
	  tmpcip -= 0x02;
	  tmport=workportu + 0x18;
	  if ( dirctu[h][tarid] != 0 )
	  {
	     outb(0x08,tmport);
	     outb(0x01,tmpcip);
	     in_int[h]=0;
	     return;
	  }
	  outb(0x08,tmport);
	  outb(0x09,tmpcip);
	  in_int[h]=0;
	  return;
       }

       workrequ=curr_req[h][tarid];
       if ( i == 0x42 )
       {
	  errstus=0x02;
	  workrequ->result=errstus;
	  goto go_42;
       }
       if ( i == 0x16 )
       {
	  errstus=0;
	  tmport -= 0x08;
	  errstus=inb(tmport);
	  workrequ->result=errstus;
/*	  if ( errstus == 0x02 )
	  {
	     tmport +=0x10;
	     if ((inb(tmport) & 0x80) != 0)
	     {
		printk(" autosense ");
	     }
	     tmport -=0x09;
	     outb(0,tmport);
	     tmport=workportu+0x3a;
	     outb((unsigned char)(inb(tmport) | 0x10),tmport);
	     tmport -= 0x39;

	     outb(0x08,tmport++);
	     outb(0x7f,tmport++);
	     outb(0x03,tmport++);
	     outb(0x00,tmport++);
	     outb(0x00,tmport++);
	     outb(0x00,tmport++);
	     outb(0x0e,tmport++);
	     outb(0x00,tmport);
	     tmport+=0x07;
	     outb(0x00,tmport++);
	     tmport++;
	     outb(devspu[h][workrequ->target],tmport++);
	     outb(0x00,tmport++);
	     outb(0x00,tmport++);
	     outb(0x0e,tmport++);
	     tmport+=0x03;
	     outb(0x09,tmport);
	     tmport+=0x07;
	     i=0;
	     adrcntu=(unsigned long)(&workrequ->sense_buffer[0]);
get_sens:
	     j=inb(tmport);
	     if ((j & 0x01) != 0)
	     {
		tmport-=0x06;
		(unsigned char)(((caddr_t) adrcntu)[i++])=inb(tmport);
		tmport+=0x06;
		goto get_sens;
	     }
	     if ((j & 0x80) == 0)
	     {
		goto get_sens;
	     }
	     if ((j & 0x40) == 0)
	     {
		tmport-=0x08;
		i=inb(tmport);
	     }
	     tmport=workportu+0x3a;
	     outb((unsigned char)(inb(tmport) & 0xef),tmport);
	     tmport=workportu+0x01;
	     outb(0x2c,tmport);
	     tmport += 0x15;
	     outb(0x80,tmport);
	  }   */
go_42:
	  (*workrequ->scsi_done)(workrequ);
	  curr_req[h][tarid]=0;
	  workingu[h]--;
	  if (wide_idu[h] != 0)
	  {
	     tmport=workportu+0x1b;
	     j=inb(tmport) & 0x0e;
	     j |= 0x01;
	     outb(j,tmport);
	  }
	  if (((last_cmd[h] != 0xff) || (quhdu[h] != quendu[h])) &&
	      (in_snd[h] == 0))
	  {
	     send_s870(h);
	  }
	  in_int[h]=0;
	  return;
       }
   if ( i == 0x4f )
   {
      i=0x89;
   }
   i &= 0x0f;
   if ( i == 0x09 )
   {
      tmpcip=tmpcip+4;
      outl(prdaddru[h][tarid],tmpcip);
      tmpcip=tmpcip-2;
      outb(0x06,tmpcip);
      outb(0x00,tmpcip);
      tmpcip=tmpcip-2;
      tmport=workportu+0x10;
      outb(0x41,tmport);
      dirctu[h][tarid]=0x00;
      tmport += 0x08;
      outb(0x08,tmport);
      outb(0x09,tmpcip);
      in_int[h]=0;
      return;
   }
   if ( i == 0x08 )
   {
      tmpcip=tmpcip+4;
      outl(prdaddru[h][tarid],tmpcip);
      tmpcip=tmpcip-2;
      outb(0x06,tmpcip);
      outb(0x00,tmpcip);
      tmpcip=tmpcip-2;
      tmport=workportu+0x10;
      outb(0x41,tmport);
      tmport += 0x05;
      outb((unsigned char)(inb(tmport) | 0x20),tmport);
      dirctu[h][tarid]=0x20;
      tmport += 0x03;
      outb(0x08,tmport);
      outb(0x01,tmpcip);
      in_int[h]=0;
      return;
   }
   tmport -= 0x07;
   if ( i == 0x0a )
   {
      outb(0x30,tmport);
   }
   else
   {
      outb(0x46,tmport);
   }
   dirctu[h][tarid]=0x00;
   tmport += 0x02;
   outb(0x00,tmport++);
   outb(0x00,tmport++);
   outb(0x00,tmport++);
   tmport+=0x03;
   outb(0x08,tmport);
   in_int[h]=0;
   return;
  }
  else
  {
     tmport=workportu+0x17;
     inb(tmport);
     workingu[h]=0;
     in_int[h]=0;
     return;
  }
}

int atp870u_queuecommand(Scsi_Cmnd * req_p, void (*done)(Scsi_Cmnd *))
{
    unsigned char i,h;
    unsigned long flags;
    unsigned short int m;
    unsigned int tmport;

    for( h=0; h <= admaxu; h++ )
    {
       if ( req_p->host == atp_host[h] )
       {
	  goto host_ok;
       }
    }
    return 0;
host_ok:
   if ( req_p->channel != 0 )
   {
      req_p->result = 0x00040000;
      done(req_p);
      return 0;
   }
   m=1;
   m=  m << req_p->target;
   if ( ( m & active_idu[h] ) == 0 )
   {
      req_p->result = 0x00040000;
      done(req_p);
      return 0;
   }
   if (done)
   {
      req_p->scsi_done = done;
   }
   else
   {
      printk("atp870u_queuecommand: done can't be NULL\n");
      req_p->result = 0;
      done(req_p);
      return 0;
   }
   quendu[h]++;
   if ( quendu[h] >= qcnt )
   {
      quendu[h]=0;
   }
   wait_que_empty:
     if ( quhdu[h] == quendu[h] )
     {
	goto wait_que_empty;
     }
     save_flags(flags);
     cli();
     querequ[h][quendu[h]]=req_p;
     if ( quendu[h] == 0 )
     {
	i=qcnt-1;
     }
     else
     {
	i=quendu[h]-1;
     }
     tmport = ioportu[h]+0x1c;
     restore_flags(flags);
     if ((inb(tmport) == 0) && (in_int[h] == 0) && (in_snd[h] == 0))
     {
	 send_s870(h);
     }
     return 0;
}

void mydlyu(unsigned int dlycnt )
{
    unsigned int i ;
    for ( i = 0 ; i < dlycnt ; i++ )
    {
       inb(0x80);
    }
}

void send_s870(unsigned char h)
{
     unsigned int  tmport;
     Scsi_Cmnd *workrequ;
     unsigned long flags;
     unsigned int   i;
     unsigned char  j,tarid;
     unsigned char  *prd;
     unsigned short int   tmpcip,w;
     unsigned long  l,bttl;
     unsigned int workportu;
     struct scatterlist * sgpnt;

	save_flags(flags);
	cli();
	if ( in_snd[h] != 0 )
	{
	   restore_flags(flags);
	   return;
	}
	in_snd[h]=1;
	if ((last_cmd[h] != 0xff) && ((last_cmd[h] & 0x40) != 0))
	{
	   last_cmd[h] &= 0x0f;
	   workrequ=curr_req[h][last_cmd[h]];
	   goto cmd_subp;
	}
	workingu[h]++;
	j=quhdu[h];
	quhdu[h]++;
	if ( quhdu[h] >= qcnt )
	{
	   quhdu[h]=0;
	}
	workrequ=querequ[h][quhdu[h]];
	if ( curr_req[h][workrequ->target] == 0 )
	{
	   curr_req[h][workrequ->target]=workrequ;
	   last_cmd[h]=workrequ->target;
	   goto cmd_subp;
	}
	quhdu[h]=j;
	workingu[h]--;
	in_snd[h]=0;
	restore_flags(flags);
	return ;
cmd_subp:
   workportu=ioportu[h];
   tmport=workportu+0x1f;
   if ((inb(tmport) & 0xb0) != 0)
   {
      goto abortsnd;
   }
   tmport=workportu+0x1c;
   if ( inb(tmport) == 0 )
   {
      goto oktosend;
   }
abortsnd:
   last_cmd[h] |= 0x40;
   in_snd[h]=0;
   restore_flags(flags);
   return;
oktosend:
   memcpy(&ata_cdbu[h][0], &workrequ->cmnd[0], workrequ->cmd_len);
   if ( ata_cdbu[h][0] == 0x25 )
   {
      if ( workrequ->request_bufflen > 8 )
      {
	 workrequ->request_bufflen=0x08;
      }
   }
   if ( ata_cdbu[h][0] == 0x12 )
   {
      if ( workrequ->request_bufflen > 0x24 )
      {
	 workrequ->request_bufflen = 0x24;
	 ata_cdbu[h][4]=0x24;
      }
   }

   tmport=workportu+0x1b;
   j=inb(tmport) & 0x0e;
   tarid=workrequ->target;
   w=1;
   w = w << tarid;
   if ((w & wide_idu[h]) != 0)
   {
      j |= 0x01;
   }
   outb(j,tmport);
   tmport=workportu;
   outb(workrequ->cmd_len,tmport++);
   outb(0x2c,tmport++);
   outb(0xcf,tmport++);
   for ( i=0 ; i < workrequ->cmd_len ; i++ )
   {
       outb(ata_cdbu[h][i],tmport++);
   }
   tmport=workportu+0x0f;
   outb(0x00,tmport);
   tmport+=0x02;
   outb(devspu[h][tarid],tmport++);
   if (workrequ->use_sg)
   {

     l=0;
     sgpnt = (struct scatterlist *) workrequ->request_buffer;
     for(i=0; i<workrequ->use_sg; i++)
     {
       if(sgpnt[i].length == 0 || workrequ->use_sg > ATP870U_SCATTER)
       {
	 panic("Foooooooood fight!");
       }
       l += sgpnt[i].length;
     }
   }
   else
   {
     l=workrequ->request_bufflen;
   }
   outb((unsigned char)(((unsigned char *)(&l))[2]),tmport++);
   outb((unsigned char)(((unsigned char *)(&l))[1]),tmport++);
   outb((unsigned char)(((unsigned char *)(&l))[0]),tmport++);
   j=tarid;
   last_lenu[h][j]=l;
   tran_lenu[h][j]=0;
   if ((j & 0x08) != 0)
   {
      j=(j & 0x07) | 0x40;
   }
   if ((ata_cdbu[h][0] == 0x0a) || (ata_cdbu[h][0] == 0x2a) ||
       (ata_cdbu[h][0] == 0xaa) || (ata_cdbu[h][0] == 0x15))
   {
      outb((unsigned char)(j | 0x20),tmport++);
   }
   else
   {
      outb(j,tmport++);
   }
   outb(0x80,tmport);
   tmport=workportu + 0x1c;
   dirctu[h][tarid]=0;
   if ( l == 0 )
   {
      if ( inb(tmport) == 0 )
      {
	 tmport=workportu+0x18;
	 outb(0x08,tmport);
      }
      else
      {
	last_cmd[h] |= 0x40;
      }
      in_snd[h]=0;
      restore_flags(flags);
      return;
   }
   tmpcip=pciportu[h];
   prd=&prd_tableu[h][tarid][0];
   prd_posu[h][tarid]=prd;
   if (workrequ->use_sg)
   {
     sgpnt = (struct scatterlist *) workrequ->request_buffer;
     i=0;
     for(j=0; j<workrequ->use_sg; j++)
     {
	(unsigned long)(((unsigned long *)(prd))[i >> 1])=(unsigned long)sgpnt[j].address;
	(unsigned short int)(((unsigned short int *)(prd))[i+2])=sgpnt[j].length;
	(unsigned short int)(((unsigned short int *)(prd))[i+3])=0;
	i +=0x04;
     }
     (unsigned short int)(((unsigned short int *)(prd))[i-1])=0x8000;
   }
   else
   {
     bttl=(unsigned long)workrequ->request_buffer;
     l=workrequ->request_bufflen;
     i=0;
     while ( l > 0x10000 )
     {
	(unsigned short int)(((unsigned short int *)(prd))[i+3])=0x0000;
	(unsigned short int)(((unsigned short int *)(prd))[i+2])=0x0000;
	(unsigned long)(((unsigned long *)(prd))[i >> 1])=bttl;
	l -= 0x10000;
	bttl += 0x10000;
	i += 0x04;
     }
     (unsigned short int)(((unsigned short int *)(prd))[i+3])=0x8000;
     (unsigned short int)(((unsigned short int *)(prd))[i+2])=l;
     (unsigned long)(((unsigned long *)(prd))[i >> 1])=bttl;
   }
   tmpcip=tmpcip+4;
   prdaddru[h][tarid]=(unsigned long)&prd_tableu[h][tarid][0];
   outl(prdaddru[h][tarid],tmpcip);
   tmpcip=tmpcip-2;
   outb(0x06,tmpcip);
   outb(0x00,tmpcip);
   tmpcip=tmpcip-2;
   if ((ata_cdbu[h][0] == 0x0a) || (ata_cdbu[h][0] == 0x2a) ||
       (ata_cdbu[h][0] == 0xaa) || (ata_cdbu[h][0] == 0x15))
   {
      dirctu[h][tarid]=0x20;
      if ( inb(tmport) == 0 )
      {
	 tmport=workportu+0x18;
	 outb(0x08,tmport);
	 outb(0x01,tmpcip);
      }
      else
      {
	 last_cmd[h] |= 0x40;
      }
      in_snd[h]=0;
      restore_flags(flags);
      return;
   }
   if ( inb(tmport) == 0 )
   {
      tmport=workportu+0x18;
      outb(0x08,tmport);
      outb(0x09,tmpcip);
   }
   else
   {
      last_cmd[h] |= 0x40;
   }
   in_snd[h]=0;
   restore_flags(flags);
   return;

}

static void internal_done(Scsi_Cmnd * SCpnt)
{
	SCpnt->SCp.Status++;
}

int atp870u_command(Scsi_Cmnd * SCpnt)
{

    atp870u_queuecommand(SCpnt, internal_done);

    SCpnt->SCp.Status = 0;
    while (!SCpnt->SCp.Status)
	barrier();
    return SCpnt->result;
}

unsigned char fun_scam ( unsigned char host,unsigned short int * val )
{
    unsigned int  tmport ;
    unsigned short int	 i,k;
    unsigned char     j;

    tmport = ioportu[host]+0x1c;
    outw(*val,tmport);
FUN_D7:
    for ( i=0; i < 10; i++ )	     /* stable >= bus settle delay(400 ns)  */
    {
	k=inw(tmport);
	j= (unsigned char)(k >> 8);
	if ((k & 0x8000) != 0)	     /* DB7 all release?    */
	{
	   goto  FUN_D7;
	}
    }
    *val |= 0x4000;		    /* assert DB6	    */
    outw(*val,tmport);
    *val &= 0xdfff;		    /* assert DB5	    */
    outw(*val,tmport);
FUN_D5:
    for ( i=0; i < 10; i++ )	    /* stable >= bus settle delay(400 ns) */
    {
       if ((inw(tmport) & 0x2000) != 0)   /* DB5 all release?	*/
       {
	  goto	FUN_D5;
       }
    }
    *val |= 0x8000;		     /* no DB4-0, assert DB7	*/
    *val &= 0xe0ff;
    outw(*val,tmport);
    *val &= 0xbfff;		     /* release DB6		*/
    outw(*val,tmport);
FUN_D6:
    for ( i=0; i < 10; i++ )	     /* stable >= bus settle delay(400 ns)  */
    {
       if ((inw(tmport) & 0x4000) != 0)   /* DB6 all release?  */
       {
	  goto	FUN_D6;
       }
    }

    return j;
}

void tscam( unsigned char host )
{

    unsigned int  tmport ;
    unsigned char  i,j,k;
    unsigned long  n;
    unsigned short int	m,assignid_map,val;
    unsigned char  mbuf[33],quintet[2];
    static unsigned char g2q_tab[8]={ 0x38,0x31,0x32,0x2b,0x34,0x2d,0x2e,0x27 };


    for ( i=0; i < 0x10; i++ )
    {
	mydlyu(0xffff);
    }

    tmport = ioportu[host]+1;
    outb(0x08,tmport++);
    outb(0x7f,tmport);
    tmport = ioportu[host]+0x11;
    outb(0x20,tmport);

    if ((scam_on[host] & 0x40) == 0)
    {
       return;
    }

    m=1;
    m <<= host_idu[host];
    j=16;
    if ( chip_veru[host] < 4 )
    {
       m |= 0xff00;
       j=8;
    }
    assignid_map=m;
    tmport = ioportu[host]+0x02;
    outb(0x02,tmport++);	/* 2*2=4ms,3EH 2/32*3E=3.9ms */
    outb(0,tmport++);
    outb(0,tmport++);
    outb(0,tmport++);
    outb(0,tmport++);
    outb(0,tmport++);
    outb(0,tmport++);

    for ( i = 0 ; i < j ; i ++ )
    {
	m=1;
	m=m<<i;
	if ( ( m & assignid_map ) != 0 )
	{
	   continue;
	}
    tmport = ioportu[host]+0x0f;
    outb(0,tmport++);
    tmport += 0x02;
    outb(0,tmport++);
    outb(0,tmport++);
    outb(0,tmport++);
    if ( i > 7 )
    {
       k=(i & 0x07) | 0x40;
    }
    else
    {
       k=i;
    }
    outb(k,tmport++);
    tmport = ioportu[host]+0x1b;
    if ( chip_veru[host] == 4 )
    {
       outb((unsigned char)((inb(tmport) & 0x0e) | 0x01),tmport);
    }
    else
    {
       outb((unsigned char)(inb(tmport) & 0x0e),tmport);
    }
wait_rdyok:
    tmport = ioportu[host]+0x18;
    outb(0x09,tmport);
    tmport += 0x07;

    while ((inb(tmport) & 0x80) == 0x00);
    tmport -= 0x08;
    k=inb(tmport);
    if ( k != 0x16 )
    {
       if ((k == 0x85) || (k == 0x42))
       {
	  continue;
       }
       tmport = ioportu[host]+0x10;
       outb(0x41,tmport);
       goto wait_rdyok;
    }
    assignid_map |= m;

    }
    tmport = ioportu[host]+0x02;
    outb(0x7f,tmport);
    tmport = ioportu[host]+0x1b;
    outb(0x02,tmport);

    outb(0,0x80);

    val=0x0080;      /* bsy  */
    tmport = ioportu[host]+0x1c;
    outw(val,tmport);
    val |=0x0040;    /* sel  */
    outw(val,tmport);
    val |=0x0004;    /* msg  */
    outw(val,tmport);
    inb(0x80);		      /* 2 deskew delay(45ns*2=90ns) */
    val &=0x007f;    /* no bsy	*/
    outw(val,tmport);
    mydlyu(0xffff);  /* recommanded SCAM selection response time */
    mydlyu(0xffff);
    val &=0x00fb;    /* after 1ms no msg */
    outw(val,tmport);
wait_nomsg:
    if ((inb(tmport) & 0x04) != 0)
    {
       goto wait_nomsg;
    }
    outb(1,0x80);
    mydlyu(100);
    for ( n=0; n < 0x30000; n++ )
    {
	if ((inb(tmport) & 0x80) != 0)	   /* bsy ? */
	{
	   goto wait_io;
	}
    }
    goto  TCM_SYNC;
wait_io:
    for ( n=0; n < 0x30000; n++ )
    {
	if ((inb(tmport) & 0x81) == 0x0081)
	{
	   goto wait_io1;
	}
    }
    goto  TCM_SYNC;
wait_io1:
    inb(0x80);
    val |=0x8003;    /* io,cd,db7  */
    outw(val,tmport);
    inb(0x80);
    val &=0x00bf;    /* no sel	   */
    outw(val,tmport);
    outb(2,0x80);
TCM_SYNC:
    mydlyu(0x800);
    if ((inb(tmport) & 0x80) == 0x00)	 /* bsy ? */
    {
       outw(0,tmport--);
       outb(0,tmport);
       tmport=ioportu[host] + 0x15;
       outb(0,tmport);
       tmport += 0x03;
       outb(0x09,tmport);
       tmport += 0x07;
       while ((inb(tmport) & 0x80) == 0);
       tmport -= 0x08;
       inb(tmport);
       return;
    }

    val &= 0x00ff;		 /* synchronization  */
    val |= 0x3f00;
    fun_scam(host,&val);
    outb(3,0x80);
    val &= 0x00ff;		 /* isolation	     */
    val |= 0x2000;
    fun_scam(host,&val);
    outb(4,0x80);
    i=8;
    j=0;
TCM_ID:
    if ((inw(tmport) & 0x2000) == 0)
    {
       goto TCM_ID;
    }
    outb(5,0x80);
    val &= 0x00ff;		 /* get ID_STRING */
    val |= 0x2000;
    k=fun_scam(host,&val);
    if ((k & 0x03) == 0)
    {
       goto TCM_5;
    }
    mbuf[j] <<= 0x01;
    mbuf[j] &= 0xfe;
    if ((k & 0x02) != 0)
    {
       mbuf[j] |= 0x01;
    }
    i--;
    if ( i > 0 )
    {
       goto TCM_ID;
    }
    j++;
    i=8;
    goto TCM_ID;

TCM_5:			     /* isolation complete..  */
/*    mbuf[32]=0;
    printk(" \n%x %x %x %s\n ",assignid_map,mbuf[0],mbuf[1],&mbuf[2]); */
    i=15;
    j=mbuf[0];
    if ((j & 0x20) != 0)     /* bit5=1:ID upto 7      */
    {
       i=7;
    }
    if ((j & 0x06) == 0)     /* IDvalid?	      */
    {
       goto  G2Q5;
    }
    k=mbuf[1];
small_id:
    m=1;
    m <<= k;
    if ((m & assignid_map) == 0)
    {
       goto G2Q_QUIN;
    }
    if ( k > 0 )
    {
       k--;
       goto small_id;
    }
G2Q5:			      /* srch from max acceptable ID#  */
    k=i;		      /* max acceptable ID#	       */
G2Q_LP:
    m=1;
    m <<= k;
    if ((m & assignid_map) == 0)
    {
       goto G2Q_QUIN;
    }
    if ( k > 0 )
    {
       k--;
       goto G2Q_LP;
    }
G2Q_QUIN:		      /* k=binID#,	 */
    assignid_map |= m;
    if ( k < 8 )
    {
       quintet[0]=0x38;       /* 1st dft ID<8	 */
    }
    else
    {
       quintet[0]=0x31;       /* 1st  ID>=8	 */
    }
    k &= 0x07;
    quintet[1]=g2q_tab[k];

    val &= 0x00ff;	       /* AssignID 1stQuintet,AH=001xxxxx  */
    m=quintet[0] << 8;
    val |= m;
    fun_scam(host,&val);
    val &= 0x00ff;	       /* AssignID 2ndQuintet,AH=001xxxxx */
    m=quintet[1] << 8;
    val |= m;
    fun_scam(host,&val);

    goto TCM_SYNC;

}

void is870(unsigned long host,unsigned int wkport )
{
    unsigned int  tmport ;
    unsigned char i,j,k,rmb;
    unsigned short int m;
    static unsigned char mbuf[512];
    static unsigned char satn[9] = { 0,0,0,0,0,0,0,6,6 };
    static unsigned char inqd[9] = { 0x12,0,0,0,0x24,0,0,0x24,6 };
    static unsigned char synn[6] = { 0x80,1,3,1,0x19,0x0e };
    static unsigned char synu[6] = { 0x80,1,3,1,0x0c,0x0e };
    static unsigned char synw[6] = { 0x80,1,3,1,0x0c,0x07 };
    static unsigned char wide[6] = { 0x80,1,2,3,1,0 };

    sync_idu=0;
    tmport=wkport+0x3a;
    outb((unsigned char)(inb(tmport) | 0x10),tmport);

    for ( i = 0 ; i < 16 ; i ++ )
    {
	if ((chip_veru[host] != 4) && (i > 7))
	{
	   break;
	}
	m=1;
	m=m<<i;
	if ( ( m & active_idu[host] ) != 0 )
	{
	   continue;
	}
	if ( i == host_idu[host] )
	{
	   printk("         ID: %2d  Host Adapter\n",host_idu[host]);
	   continue;
	}
	if ( chip_veru[host] == 4 )
	{
	   tmport=wkport+0x1b;
	   j=(inb(tmport) & 0x0e) | 0x01;
	   outb(j,tmport);
	}
    tmport=wkport+1;
    outb(0x08,tmport++);
    outb(0x7f,tmport++);
    outb(satn[0],tmport++);
    outb(satn[1],tmport++);
    outb(satn[2],tmport++);
    outb(satn[3],tmport++);
    outb(satn[4],tmport++);
    outb(satn[5],tmport++);
    tmport+=0x06;
    outb(0,tmport);
    tmport+=0x02;
    outb(devspu[host][i],tmport++);
    outb(0,tmport++);
    outb(satn[6],tmport++);
    outb(satn[7],tmport++);
    j=i;
    if ((j & 0x08) != 0)
    {
       j=(j & 0x07) | 0x40;
    }
    outb(j,tmport);
    tmport+=0x03;
    outb(satn[8],tmport);
    tmport+=0x07;

    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e))
    {
       continue;
    }
    while ( inb(tmport) != 0x8e );
    active_idu[host] |= m;

    tmport=wkport+0x10;
    outb(0x30,tmport);
    tmport=wkport+0x04;
    outb(0x00,tmport);

phase_cmd:
    tmport=wkport+0x18;
    outb(0x08,tmport);
    tmport+=0x07;
    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    j=inb(tmport);
    if ( j != 0x16 )
    {
       tmport=wkport+0x10;
       outb(0x41,tmport);
       goto phase_cmd;
    }
sel_ok:
       tmport=wkport+3;
       outb(inqd[0],tmport++);
       outb(inqd[1],tmport++);
       outb(inqd[2],tmport++);
       outb(inqd[3],tmport++);
       outb(inqd[4],tmport++);
       outb(inqd[5],tmport);
       tmport+=0x07;
       outb(0,tmport);
       tmport+=0x02;
       outb(devspu[host][i],tmport++);
       outb(0,tmport++);
       outb(inqd[6],tmport++);
       outb(inqd[7],tmport++);
       tmport+=0x03;
       outb(inqd[8],tmport);
       tmport+=0x07;
       while ((inb(tmport) & 0x80) == 0x00);
       tmport-=0x08;
       if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e))
       {
	  continue;
       }
       while ( inb(tmport) != 0x8e );
       if ( chip_veru[host] == 4 )
       {
	  tmport=wkport+0x1b;
	  j=inb(tmport) & 0x0e;
	  outb(j,tmport);
       }
       tmport=wkport+0x18;
       outb(0x08,tmport);
       tmport += 0x07;
       j=0;
rd_inq_data:
       k=inb(tmport);
       if ((k & 0x01) != 0 )
       {
	  tmport-=0x06;
	  mbuf[j++]=inb(tmport);
	  tmport+=0x06;
	  goto rd_inq_data;
       }
       if ((k & 0x80) == 0 )
       {
	  goto rd_inq_data;
       }
       tmport-=0x08;
       j=inb(tmport);
       if ( j == 0x16 )
       {
	  goto inq_ok;
       }
    tmport=wkport+0x10;
    outb(0x46,tmport);
    tmport+=0x02;
    outb(0,tmport++);
    outb(0,tmport++);
    outb(0,tmport++);
    tmport+=0x03;
    outb(0x08,tmport);
    tmport+=0x07;
    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    if (inb(tmport) != 0x16)
    {
       goto sel_ok;
    }
inq_ok:
     mbuf[36]=0;
     printk("         ID: %2d  %s\n",i,&mbuf[8]);
     devtypeu[host][i]=mbuf[0];
     rmb=mbuf[1];
     if ( chip_veru[host] != 4 )
     {
	goto not_wide;
     }
     if ((mbuf[7] & 0x60) == 0)
     {
	goto not_wide;
     }
     if ((global_map[host] & 0x20) == 0)
     {
	goto not_wide;
     }
     tmport=wkport+0x1b;
     j=(inb(tmport) & 0x0e) | 0x01;
     outb(j,tmport);
    tmport=wkport+3;
    outb(satn[0],tmport++);
    outb(satn[1],tmport++);
    outb(satn[2],tmport++);
    outb(satn[3],tmport++);
    outb(satn[4],tmport++);
    outb(satn[5],tmport++);
    tmport+=0x06;
    outb(0,tmport);
    tmport+=0x02;
    outb(devspu[host][i],tmport++);
    outb(0,tmport++);
    outb(satn[6],tmport++);
    outb(satn[7],tmport++);
    tmport+=0x03;
    outb(satn[8],tmport);
    tmport+=0x07;

    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e))
    {
       continue;
    }
    while ( inb(tmport) != 0x8e );
try_wide:
    j=0;
    tmport=wkport+0x14;
    outb(0x05,tmport);
    tmport += 0x04;
    outb(0x20,tmport);
    tmport+=0x07;

    while ((inb(tmport) & 0x80) == 0 )
    {
       if ((inb(tmport) & 0x01) != 0 )
       {
	  tmport-=0x06;
	  outb(wide[j++],tmport);
	  tmport+=0x06;
       }
    }
    tmport-=0x08;
    while ((inb(tmport) & 0x80) == 0x00);
    j=inb(tmport) & 0x0f;
    if ( j == 0x0f )
    {
       goto widep_in;
    }
    if ( j == 0x0a )
    {
       goto widep_cmd;
    }
    if ( j == 0x0e )
    {
       goto try_wide;
    }
    continue;
widep_out:
    tmport=wkport+0x18;
    outb(0x20,tmport);
    tmport+=0x07;
    while ((inb(tmport) & 0x80) == 0 )
    {
	if ((inb(tmport) & 0x01) != 0 )
	{
	   tmport-=0x06;
	   outb(0,tmport);
	   tmport+=0x06;
	}
    }
    tmport-=0x08;
    j=inb(tmport) & 0x0f;
    if ( j == 0x0f )
    {
       goto widep_in;
    }
    if ( j == 0x0a )
    {
       goto widep_cmd;
    }
    if ( j == 0x0e )
    {
       goto widep_out;
    }
    continue;
widep_in:
    tmport=wkport+0x14;
    outb(0xff,tmport);
    tmport += 0x04;
    outb(0x20,tmport);
    tmport+=0x07;
    k=0;
widep_in1:
    j=inb(tmport);
    if ((j & 0x01) != 0)
    {
       tmport-=0x06;
       mbuf[k++]=inb(tmport);
       tmport+=0x06;
       goto widep_in1;
    }
    if ((j & 0x80) == 0x00)
    {
       goto widep_in1;
    }
    tmport-=0x08;
    j=inb(tmport) & 0x0f;
    if ( j == 0x0f )
    {
       goto widep_in;
    }
    if ( j == 0x0a )
    {
       goto widep_cmd;
    }
    if ( j == 0x0e )
    {
       goto widep_out;
    }
    continue;
widep_cmd:
    tmport=wkport+0x10;
    outb(0x30,tmport);
    tmport=wkport+0x14;
    outb(0x00,tmport);
    tmport+=0x04;
    outb(0x08,tmport);
    tmport+=0x07;
    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    j=inb(tmport);
    if ( j != 0x16 )
    {
       if ( j == 0x4e )
       {
	  goto widep_out;
       }
       continue;
    }
    if ( mbuf[0] != 0x01 )
    {
       goto not_wide;
    }
    if ( mbuf[1] != 0x02 )
    {
       goto not_wide;
    }
    if ( mbuf[2] != 0x03 )
    {
       goto not_wide;
    }
    if ( mbuf[3] != 0x01 )
    {
       goto not_wide;
    }
    m=1;
    m = m << i;
    wide_idu[host] |= m;
not_wide:
    if ((devtypeu[host][i] == 0x00) || (devtypeu[host][i] == 0x07))
    {
       goto set_sync;
    }
    continue;
set_sync:
    tmport=wkport+0x1b;
    j=inb(tmport) & 0x0e;
    if ((m & wide_idu[host]) != 0 )
    {
       j |= 0x01;
    }
    outb(j,tmport);
    tmport=wkport+3;
    outb(satn[0],tmport++);
    outb(satn[1],tmport++);
    outb(satn[2],tmport++);
    outb(satn[3],tmport++);
    outb(satn[4],tmport++);
    outb(satn[5],tmport++);
    tmport+=0x06;
    outb(0,tmport);
    tmport+=0x02;
    outb(devspu[host][i],tmport++);
    outb(0,tmport++);
    outb(satn[6],tmport++);
    outb(satn[7],tmport++);
    tmport+=0x03;
    outb(satn[8],tmport);
    tmport+=0x07;

    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    if ((inb(tmport) != 0x11) && (inb(tmport) != 0x8e))
    {
       continue;
    }
    while ( inb(tmport) != 0x8e);
try_sync:
    j=0;
    tmport=wkport+0x14;
    outb(0x06,tmport);
    tmport += 0x04;
    outb(0x20,tmport);
    tmport+=0x07;

    while ((inb(tmport) & 0x80) == 0 )
    {
       if ((inb(tmport) & 0x01) != 0 )
       {
	  tmport-=0x06;
	  if ( rmb != 0 )
	  {
	     outb(synn[j++],tmport);
	  }
	  else
	  {
	     if ((m & wide_idu[host]) != 0)
	     {
		outb(synw[j++],tmport);
	     }
	     else
	     {
		if ((m & ultra_map[host]) != 0)
		{
		   outb(synu[j++],tmport);
		}
		else
		{
		   outb(synn[j++],tmport);
		}
	     }
	  }
	  tmport+=0x06;
       }
    }
    tmport-=0x08;
    while ((inb(tmport) & 0x80) == 0x00);
    j=inb(tmport) & 0x0f;
    if ( j == 0x0f )
    {
       goto phase_ins;
    }
    if ( j == 0x0a )
    {
       goto phase_cmds;
    }
    if ( j == 0x0e )
    {
       goto try_sync;
    }
    continue;
phase_outs:
    tmport=wkport+0x18;
    outb(0x20,tmport);
    tmport+=0x07;
    while ((inb(tmport) & 0x80) == 0x00)
    {
      if ((inb(tmport) & 0x01) != 0x00)
      {
	 tmport-=0x06;
	 outb(0x00,tmport);
	 tmport+=0x06;
      }
    }
    tmport-=0x08;
    j=inb(tmport);
    if ( j == 0x85 )
    {
       goto tar_dcons;
    }
    j &= 0x0f;
    if ( j == 0x0f )
    {
       goto phase_ins;
    }
    if ( j == 0x0a )
    {
       goto phase_cmds;
    }
    if ( j == 0x0e )
    {
       goto phase_outs;
    }
    continue;
phase_ins:
    tmport=wkport+0x14;
    outb(0xff,tmport);
    tmport += 0x04;
    outb(0x20,tmport);
    tmport+=0x07;
    k=0;
phase_ins1:
    j=inb(tmport);
    if ((j & 0x01) != 0x00)
    {
       tmport-=0x06;
       mbuf[k++]=inb(tmport);
       tmport+=0x06;
       goto phase_ins1;
    }
    if ((j & 0x80) == 0x00)
    {
       goto phase_ins1;
    }
    tmport-=0x08;
    while ((inb(tmport) & 0x80) == 0x00);
    j=inb(tmport);
    if ( j == 0x85 )
    {
       goto tar_dcons;
    }
    j &= 0x0f;
    if ( j == 0x0f )
    {
       goto phase_ins;
    }
    if ( j == 0x0a )
    {
       goto phase_cmds;
    }
    if ( j == 0x0e )
    {
       goto phase_outs;
    }
    continue;
phase_cmds:
    tmport=wkport+0x10;
    outb(0x30,tmport);
tar_dcons:
    tmport=wkport+0x14;
    outb(0x00,tmport);
    tmport+=0x04;
    outb(0x08,tmport);
    tmport+=0x07;
    while ((inb(tmport) & 0x80) == 0x00);
    tmport-=0x08;
    j=inb(tmport);
    if ( j != 0x16 )
    {
       continue;
    }
    if ( mbuf[0] != 0x01 )
    {
       continue;
    }
    if ( mbuf[1] != 0x03 )
    {
       continue;
    }
    if ( mbuf[4] == 0x00 )
    {
       continue;
    }
    if ( mbuf[3] > 0x64 )
    {
       continue;
    }
    if ( mbuf[4] > 0x0c )
    {
       mbuf[4]=0x0c;
    }
    devspu[host][i] = mbuf[4];
    if ((mbuf[3] < 0x0d) && (rmb == 0))
    {
       j=0xa0;
       goto set_syn_ok;
    }
    if ( mbuf[3] < 0x1a )
    {
       j=0x20;
       goto set_syn_ok;
    }
    if ( mbuf[3] < 0x33 )
    {
       j=0x40;
       goto set_syn_ok;
    }
    if ( mbuf[3] < 0x4c )
    {
       j=0x50;
       goto set_syn_ok;
    }
    j=0x60;
set_syn_ok:
    devspu[host][i] = (devspu[host][i] & 0x0f) | j;
   }
   tmport=wkport+0x3a;
   outb((unsigned char)(inb(tmport) & 0xef),tmport);
}

/* return non-zero on detection */
int atp870u_detect(Scsi_Host_Template * tpnt)
{
    unsigned char irq,h,k;
    unsigned long flags;
    unsigned int base_io,error,tmport;
    unsigned short index = 0;
    unsigned char pci_bus[3], pci_device_fn[3], chip_ver[3],host_id;
    struct Scsi_Host * shpnt = NULL;
    int count = 0;
    static unsigned short devid[7]={0x8002,0x8010,0x8020,0x8030,0x8040,0x8050,0};
	static struct pci_dev *pdev = NULL;

    printk("aec671x_detect: \n");
    if (!pci_present())
    {
       printk("   NO BIOS32 SUPPORT.\n");
       return count;
    }

    tpnt->proc_dir = &proc_scsi_atp870u;

    for ( h = 0 ; h < 2 ; h++ )
    {
      active_idu[h]=0;
      wide_idu[h]=0;
      host_idu[h]=0x07;
      quhdu[h]=0;
      quendu[h]=0;
      pci_bus[h]=0;
      pci_device_fn[h]=0xff;
      chip_ver[h]=0;
      last_cmd[h]=0xff;
      in_snd[h]=0;
      in_int[h]=0;
      for ( k = 0 ; k < qcnt ; k++ )
      {
	  querequ[h][k]=0;
      }
      for ( k = 0 ; k < 16 ; k++ )
      {
	  curr_req[h][k]=0;
      }
    }
    h=0;
    while ( devid[h] != 0 )
    {
      pci_find_device(0x1191,devid[h],pdev);
      if (pdev == NULL); {
	  h++;
	  index=0;
	  continue;
       }
       chip_ver[2]=0;
	   
       /* To avoid messing with the things below...  */
       pci_device_fn[2] =  pdev->devfn;
       pci_bus[2] = pdev->bus->number;

       if ( devid[h] == 0x8002 )
       {
	  error = pci_read_config_byte(pdev,0x08,&chip_ver[2]);
	  if ( chip_ver[2] < 2 )
	  {
	     goto nxt_devfn;
	  }
       }
       if ( devid[h] == 0x8010 )
       {
	  chip_ver[2]=0x04;
       }
       if ( pci_device_fn[2] < pci_device_fn[0] )
       {
	  pci_bus[1]=pci_bus[0];
	  pci_device_fn[1]=pci_device_fn[0];
	  chip_ver[1]=chip_ver[0];
	  pci_bus[0]=pci_bus[2];
	  pci_device_fn[0]=pci_device_fn[2];
	  chip_ver[0]=chip_ver[2];
       }
       else if ( pci_device_fn[2] < pci_device_fn[1] )
       {
	  pci_bus[1]=pci_bus[2];
	  pci_device_fn[1]=pci_device_fn[2];
	  chip_ver[1]=chip_ver[2];
       }
nxt_devfn:
       index++;
       if ( index > 3 )
       {
	  index=0;
	  h++;
       }
    }
    for ( h=0; h < 2; h++ )
    {
    if ( pci_device_fn[h] == 0xff )
    {
       return count;
    }

    pdev->devfn = pci_device_fn[h];
    pdev->bus->number = pci_bus[h];

    /* Found an atp870u/w. */
    error = pci_read_config_dword(pdev,0x10,&base_io);
    error += pci_read_config_byte(pdev,0x3c,&irq);
    error += pci_read_config_byte(pdev,0x49,&host_id);

    base_io &= 0xfffffff8;
    printk("   ACARD AEC-671X PCI Ultra/W SCSI-3 Host Adapter: %d    IO:%x, IRQ:%d.\n"
			     ,h,base_io,irq);
    ioportu[h]=base_io;
    pciportu[h]=base_io + 0x20;
    irqnumu[h]=irq;
    host_id &= 0x07;
    host_idu[h]=host_id;
    chip_veru[h]=chip_ver[h];

    tmport=base_io+0x22;
    scam_on[h]=inb(tmport);
    tmport += 0x0b;
    global_map[h]=inb(tmport++);
    ultra_map[h]=inw(tmport);
    if ( ultra_map[h] == 0 )
    {
       scam_on[h]=0x00;
       global_map[h]=0x20;
       ultra_map[h]=0xffff;
    }

    shpnt = scsi_register(tpnt,4);

    save_flags(flags);
    cli();
    if (request_irq(irq,atp870u_intr_handle, 0, "atp870u", NULL))
    {
       printk("Unable to allocate IRQ for Acard controller.\n");
       goto unregister;
    }

    tmport=base_io+0x3a;
    k=(inb(tmport) & 0xf3) | 0x10;
    outb(k,tmport);
    outb((k & 0xdf),tmport);
    mydlyu(0x8000);
    outb(k,tmport);
    mydlyu(0x8000);
    tmport=base_io;
    outb((host_id | 0x08),tmport);
    tmport += 0x18;
    outb(0,tmport);
    tmport += 0x07;
    while ((inb(tmport) & 0x80) == 0);
    tmport -= 0x08;
    inb(tmport);
    tmport = base_io +1;
    outb(8,tmport++);
    outb(0x7f,tmport);
    tmport = base_io + 0x11;
    outb(0x20,tmport);

    tscam(h);
    is870(h,base_io);
    tmport=base_io+0x3a;
    outb((inb(tmport) & 0xef),tmport);

    atp_host[h] = shpnt;
    if ( chip_ver[h] == 4 )
    {
       shpnt->max_id = 16;
    }
    shpnt->this_id = host_id;
    shpnt->unique_id = base_io;
    shpnt->io_port = base_io;
    shpnt->n_io_port = 0x40;  /* Number of bytes of I/O space used */
    shpnt->irq = irq;
    restore_flags(flags);
    request_region(base_io, 0x40,"atp870u");  /* Register the IO ports that we use */
    count++;
    index++;
    continue;
unregister:
    scsi_unregister(shpnt);
    restore_flags(flags);
    index++;
    continue;
    }

    return count;
}

/* The abort command does not leave the device in a clean state where
   it is available to be used again.  Until this gets worked out, we will
   leave it commented out.  */

int atp870u_abort(Scsi_Cmnd * SCpnt)
{
    unsigned char h,j;
    unsigned int  tmport;
/*    printk(" atp870u_abort: \n");   */
    for ( h=0; h <= admaxu; h++ )
    {
	if ( SCpnt->host == atp_host[h] )
	{
	   goto find_adp;
	}
    }
    panic("Abort host not found !");
find_adp:
    printk(" workingu=%x last_cmd=%x ",workingu[h],last_cmd[h]);
    printk(" quhdu=%x quendu=%x ",quhdu[h],quendu[h]);
    tmport=ioportu[h];
    for ( j=0; j < 0x17; j++)
    {
	printk(" r%2x=%2x",j,inb(tmport++));
    }
    tmport += 0x05;
    printk(" r1c=%2x",inb(tmport));
    tmport += 0x03;
    printk(" r1f=%2x in_snd=%2x ",inb(tmport),in_snd[h]);
    tmport++;
    printk(" r20=%2x",inb(tmport));
    tmport += 0x02;
    printk(" r22=%2x \n",inb(tmport));
    return (SCSI_ABORT_SNOOZE);
}

int atp870u_reset(Scsi_Cmnd * SCpnt, unsigned int reset_flags)
{
    unsigned char h;
    /*
     * See if a bus reset was suggested.
     */
/*    printk("atp870u_reset: \n");    */
    for( h=0; h <= admaxu; h++ )
    {
       if ( SCpnt->host == atp_host[h] )
       {
	  goto find_host;
       }
    }
    panic("Reset bus host not found !");
find_host:
/*    SCpnt->result = 0x00080000;
    SCpnt->scsi_done(SCpnt);
    workingu[h]=0;
    quhdu[h]=0;
    quendu[h]=0;
    return (SCSI_RESET_SUCCESS | SCSI_RESET_BUS_RESET);  */
    return (SCSI_RESET_SNOOZE);
}

const char *
atp870u_info(struct Scsi_Host *notused)
{
  static char buffer[128];

  strcpy(buffer, "ACARD AEC-6710/6712 PCI Ultra/W SCSI-3 Adapter Driver V1.0 ");

  return buffer;
}

int
atp870u_set_info(char *buffer, int length, struct Scsi_Host *HBAptr)
{
  return (-ENOSYS);  /* Currently this is a no-op */
}

#define BLS buffer + len + size
int
atp870u_proc_info(char *buffer, char **start, off_t offset, int length,
    int hostno, int inout)
{
  struct Scsi_Host *HBAptr;
  static u8 buff[512];
  int	i;
  int	size = 0;
  int	len = 0;
  off_t begin = 0;
  off_t pos = 0;

  HBAptr = NULL;
  for (i = 0; i < 2; i++)
  {
    if ((HBAptr = atp_host[i]) != NULL)
    {
      if (HBAptr->host_no == hostno)
      {
	break;
      }
      HBAptr = NULL;
    }
  }

  if (HBAptr == NULL)
  {
    size += sprintf(BLS, "Can't find adapter for host number %d\n", hostno);
    len += size; pos = begin + len; size = 0;
    goto stop_output;
  }

  if (inout == TRUE) /* Has data been written to the file? */
  {
    return (atp870u_set_info(buffer, length, HBAptr));
  }

  if (offset == 0)
  {
    memset(buff, 0, sizeof(buff));
  }

  size += sprintf(BLS, "ACARD AEC-671X Driver Version: 1.0\n");
  len += size; pos = begin + len; size = 0;

  size += sprintf(BLS, "\n");
  size += sprintf(BLS, "Adapter Configuration:\n");
  size += sprintf(BLS, "               Base IO: %#.4lx\n", HBAptr->io_port);
  size += sprintf(BLS, "                   IRQ: %d\n", HBAptr->irq);
  len += size; pos = begin + len; size = 0;

stop_output:
  *start = buffer + (offset - begin);	/* Start of wanted data */
  len -= (offset - begin);	/* Start slop */
  if (len > length)
  {
    len = length;		/* Ending slop */
  }

  return (len);
}

#include "sd.h"

int atp870u_biosparam(Scsi_Disk * disk, kdev_t dev, int * ip)
{
  int heads, sectors, cylinders;

  heads = 64;
  sectors = 32;
  cylinders = disk->capacity / (heads * sectors);

  if ( cylinders > 1024 )
  {
    heads = 255;
    sectors = 63;
    cylinders = disk->capacity / (heads * sectors);
  }

  ip[0] = heads;
  ip[1] = sectors;
  ip[2] = cylinders;

  return 0;
}

#ifdef MODULE
Scsi_Host_Template driver_template = ATP870U;

#include "scsi_module.c"
#endif

