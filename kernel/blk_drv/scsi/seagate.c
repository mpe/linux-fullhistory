/*
 *	seagate.c Copyright (C) 1992 Drew Eckhardt 
 *	low level scsi driver for ST01/ST02 by
 *		Drew Eckhardt 
 *
 *	<drew@colorado.edu>
 */

#include <linux/config.h>

#ifdef CONFIG_SCSI_SEAGATE
#include <linux/sched.h>

#include "seagate.h"
#include "scsi.h"
#include "hosts.h"

static int incommand;			/*
						set if arbitration has finished and we are 
						in some command phase.
					*/

static void *base_address = NULL;	/*
						Where the card ROM starts,
						used to calculate memory mapped
						register location.
					*/
static volatile int abort_confirm = 0;

volatile void *st0x_cr_sr;       /*
						control register write,
						status register read.
						256 bytes in length.

						Read is status of SCSI BUS,
						as per STAT masks.

					*/


static volatile void *st0x_dr;         /*
						data register, read write
						256 bytes in length.
					*/


static volatile int st0x_aborted=0;	/* 
						set when we are aborted, ie by a time out, etc.
					*/

					/*
						In theory, we have a nice auto
						detect routine - but this 
						overides it. 
					*/

			
#define retcode(result) (((result) << 16) | (message << 8) | status) 			
#define STATUS (*(unsigned char *) st0x_cr_sr)
#define CONTROL STATUS 
#define DATA (*(unsigned char *) st0x_dr)

#ifndef OVERRIDE		
static const char *  seagate_bases[] = {(char *) 0xc8000, (char *) 0xca000, (char *) 0xcc000, (char *) 0xce000, (char *) 0xce000,
				        (char *) 0xdc000, (char *) 0xde000};
typedef struct 
	{
	char *signature ;
	unsigned offset;
	unsigned length;
	} Signature;
	
static const Signature signatures[] = {
{"SCSI BIOS 2.00  (C) Copyright 1987 Seagate", 15, 40},
{"SEAGATE SCSI BIOS ",16, 17},
{"SEAGATE SCSI BIOS ",17, 17}};
/*
	Note that the last signature handles BIOS revisions 3.0.0 and 
	3.2 - the real ID's are 

SEAGATE SCSI BIOS REVISION 3.0.0
SEAGATE SCSI BIOS REVISION 3.2

*/

#define NUM_SIGNATURES (sizeof(signatures) / sizeof(Signature))
#endif

int seagate_st0x_detect (int hostnum)
	{
	#ifndef OVERRIDE
		int i,j;
	#endif

	/*
		First, we try for the manual override.
	*/
	#ifdef DEBUG 
		printk("Autodetecting seagate ST0x\n");
	#endif
	
	base_address = NULL;
	#ifdef OVERRIDE
		base_address = (void *) OVERRIDE;	
		#ifdef DEBUG
			printk("Base address overridden to %x\n", base_address);
		#endif
	#else	
	/*
		To detect this card, we simply look for the SEAGATE SCSI
		from the BIOS version notice in all the possible locations
		of the ROM's.
	*/

	for (i = 0; i < (sizeof (seagate_bases) / sizeof (char  * )); ++i)
		for (j = 0; !base_address && j < NUM_SIGNATURES; ++j)
		if (!memcmp ((void *) (seagate_bases[i] +
		    signatures[j].offset), (void *) signatures[j].signature,
		    signatures[j].length))
			base_address = (void *) seagate_bases[i];
       	#endif
 
	if (base_address)
		{
		st0x_cr_sr =(void *) (((unsigned char *) base_address) + 0x1a00); 
		st0x_dr = (void *) (((unsigned char *) base_address )+ 0x1c00);
		#ifdef DEBUG
			printk("ST0x detected. Base address = %x, cr = %x, dr = %x\n", base_address, st0x_cr_sr, st0x_dr);
		#endif
		return -1;
		}
	else
		{
		#ifdef DEBUG
			printk("ST0x not detected.\n");
		#endif
		return 0;
		}
	}
	 
	

char *seagate_st0x_info(void)
{
	static char buffer[] = "Seagate ST-0X SCSI driver by Drew Eckhardt \n"
"$Header: /usr/src/linux/kernel/blk_drv/scsi/RCS/seagate.c,v 1.1 1992/04/24 18:01:50 root Exp root $\n";
	return buffer;
}



int seagate_st0x_command(unsigned char target, const void *cmnd,
			 void *buff, int bufflen)
	{
	int len;			
	unsigned char *data;	

	int clock;			/*
						We use clock for timeouts, etc.   This replaces the 
						seagate_st0x_timeout that we had been using.
					*/
	#if (DEBUG & PHASE_SELECTION)
		int temp;
	#endif

	#if (DEBUG & PHASE_EXIT)
		 void *retaddr, *realretaddr;
	#endif

	#if ((DEBUG & PHASE_ETC) || (DEBUG & PRINT_COMMAND) || (DEBUG & PHASE_EXIT))	
		int i;
	#endif

	#if (DEBUG & PHASE_ETC)
		int phase=0, newphase;
	#endif

	int done = 0;
	unsigned char status = 0;	
	unsigned char message = 0;
	register unsigned char status_read;

	#if (DEBUG & PHASE_EXIT)
		 __asm__("
movl 4(%%ebp), %%eax 
":"=a" (realretaddr):);
		printk("return address = %08x\n", realretaddr);
	#endif


	len=bufflen;
	data=(unsigned char *) buff;

	incommand = 0;
	st0x_aborted = 0;

	#if (DEBUG & PRINT_COMMAND)
		printk ("seagate_st0x_command, target = %d, command = ", target);
		for (i = 0; i < COMMAND_SIZE(((unsigned char *)cmnd)[0]); ++i)
			printk("%02x ",  ((unsigned char *) cmnd)[i]);
		printk("\n");
	#endif
	
	if (target > 6)
		return DID_BAD_TARGET;

	
	#if (DEBUG & PHASE_BUS_FREE)
		printk ("SCSI PHASE = BUS FREE \n");
	#endif

	/*

		BUS FREE PHASE

		On entry, we make sure that the BUS is in a BUS FREE
		phase, by insuring that both BSY and SEL are low for
		at least one bus settle delay.  The standard requires a
		minimum of 400 ns, which is 16 clock cycles on a
		386-40  .

		This doesn't give us much time - so we'll do two several
		reads to be sure be sure.
	*/

	clock = jiffies + ST0X_BUS_FREE_DELAY;	

	while (((STATUS |  STATUS | STATUS) & 
	         (STAT_BSY | STAT_SEL)) && 
		 (!st0x_aborted) && (jiffies < clock));

	if (jiffies > clock)
		return retcode(DID_BUS_BUSY);
	else if (st0x_aborted)
		return retcode(st0x_aborted);

	/*
		Bus free has been detected, within BUS settle.  I used to support an arbitration
		phase - however, on the seagate, this degraded performance by a factor > 10 - so
	        it is no more.
	*/

	/*
		SELECTION PHASE

		Now, we select the disk, giving it the SCSI ID at data
		and a command of PARITY if necessary, plus driver enable,
		plus raise select signal.
	*/

	#if (DEBUG & PHASE_SELECTION)
		printk("SCSI PHASE = SELECTION\n");
	#endif

	clock = jiffies + ST0X_SELECTION_DELAY;
	DATA = (unsigned char) (1 << target);

	CONTROL =  BASE_CMD | CMD_DRVR_ENABLE | CMD_SEL;

	/*
		When the SCSI device decides that we're gawking at it, it will respond by asserting BUSY on the bus.
	*/
	while (!((status_read = STATUS) & STAT_BSY) && (jiffies < clock) && !st0x_aborted)

#if (DEBUG & PHASE_SELECTION)
		{
		temp = clock - jiffies;

		if (!(jiffies % 5))
			printk("seagate_st0x_timeout : %d            \r",temp);
	
		}
		printk("Done.                                             \n\r");
		printk("Status = %02x, seagate_st0x_timeout = %d, aborted = %02x \n", status_read, temp,
			st0x_aborted);
#else
		;
#endif
	

	if ((jiffies > clock)  || (!st0x_aborted & !(status_read & STAT_BSY)))
		{
		#if (DEBUG & PHASE_SELECT)
			printk ("NO CONNECT with target %d, status = %x \n", target, STATUS);
		#endif
		return retcode(DID_NO_CONNECT);
		}

	/*
		If we have been aborted, and we have a command in progress, IE the target still has
		BSY asserted, then we will reset the bus, and notify the midlevel driver to
		expect sense.
	*/

	if (st0x_aborted)
		{
		CONTROL = BASE_CMD;
		if (STATUS & STAT_BSY)
			{
			seagate_st0x_reset();
			return retcode(DID_RESET);
			}
		
		return retcode(st0x_aborted);
		}	
	
	/*
		COMMAND PHASE
		The device has responded with a BSY, so we may now enter
		the information transfer phase, where we will send / recieve
		data and command as directed by the target.


		The nasty looking read / write inline assembler loops we use for 
		DATAIN and DATAOUT phases are approximately 4-5 times as fast as 
		the 'C' versions - since we're moving 1024 bytes of data, this
		really adds up.
	*/

	#if (DEBUG & PHASE_ETC)
		printk("PHASE = information transfer\n");
	#endif  

	incommand = 1;

	/*
		Enable command
	*/

	CONTROL = BASE_CMD | CMD_DRVR_ENABLE;

	/*
		Now, we poll the device for status information,
		and handle any requests it makes.  Note that since we are unsure of 
		how much data will be flowing across the system, etc and cannot 
		make reasonable timeouts, that we will instead have the midlevel
		driver handle any timeouts that occur in this phase.
	*/

	while (((status_read = STATUS) & STAT_BSY) && !st0x_aborted && !done) 
			{
			#ifdef PARITY
				if (status_read & STAT_PARITY)
					{
					done = 1;
					st0x_aborted = DID_PARITY;
					}	
			#endif

			if (status_read & STAT_REQ)
				{
				#if (DEBUG & PHASE_ETC)
					if ((newphase = (status_read & REQ_MASK)) != phase)
						{
						phase = newphase;
						switch (phase)
							{
							case REQ_DATAOUT : printk("SCSI PHASE = DATA OUT\n"); break;
							case REQ_DATAIN : printk("SCSI PHASE = DATA IN\n"); break;
							case REQ_CMDOUT : printk("SCSI PHASE = COMMAND OUT\n"); break;
							case REQ_STATIN : printk("SCSI PHASE = STATUS IN\n"); break;
							case REQ_MSGOUT : printk("SCSI PHASE = MESSAGE OUT\n"); break;
							case REQ_MSGIN : printk("SCSI PHASE = MESSAGE IN\n"); break;
							default : printk("UNKNOWN PHASE"); st0x_aborted = 1; done = 1;
							}	
						}
				#endif

				switch (status_read & REQ_MASK)
					{			
					case REQ_DATAOUT : 

	/*
		We loop as long as we are in a data out phase, there is data to send, and BSY is still
		active
	*/
							__asm__ ("

/*
	Local variables : 
	len = ecx
	data = esi
	st0x_cr_sr = ebx
	st0x_dr =  edi

	Test for any data here at all.
*/
	movl %0, %%esi		/* local value of data */
	movl %1, %%ecx		/* local value of len */	
	orl %%ecx, %%ecx
	jz 2f

	cld

	movl _st0x_cr_sr, %%ebx
	movl _st0x_dr, %%edi
	
1:	movb (%%ebx), %%al
/*
	Test for BSY
*/

	test $1, %%al 
	jz 2f

/*
	Test for data out phase - STATUS & REQ_MASK should be REQ_DATAOUT, which is 0.
*/
	test $0xe, %%al
	jnz 2f	
/*
	Test for REQ
*/	
	test $0x10, %%al
	jz 1b
	lodsb
	movb %%al, (%%edi) 
	loop 1b

2: 
	movl %%esi, %2
	movl %%ecx, %3
									":
/* output */
"=r" (data), "=r" (len) :
/* input */
"0" (data), "1" (len) :
/* clobbered */
"ebx", "ecx", "edi", "esi"); 

							break;

      					case REQ_DATAIN : 
	/*
		We loop as long as we are in a data out phase, there is room to read, and BSY is still
		active
	*/
 
							__asm__ ("
/*
	Local variables : 
	ecx = len
	edi = data
	esi = st0x_cr_sr
	ebx = st0x_dr

	Test for room to read
*/

	movl %0, %%edi		/* data */
	movl %1, %%ecx		/* len */
	orl %%ecx, %%ecx
	jz 2f

	cld
	movl _st0x_cr_sr, %%esi
	movl _st0x_dr, %%ebx

1:	movb (%%esi), %%al
/*
	Test for BSY
*/

	test $1, %%al 
	jz 2f

/*
	Test for data in phase - STATUS & REQ_MASK should be REQ_DATAIN, = STAT_IO, which is 4.
*/
	movb $0xe, %%ah	
	andb %%al, %%ah
	cmpb $0x04, %%ah
	jne 2f
		
/*
	Test for REQ
*/	
	test $0x10, %%al
	jz 1b

	movb (%%ebx), %%al	
	stosb	
	loop 1b

2: 	movl %%edi, %2	 	/* data */
	movl %%ecx, %3 		/* len */
									":
/* output */
"=r" (data), "=r" (len) :
/* input */
"0" (data), "1" (len) :
/* clobbered */
"ebx", "ecx", "edi", "esi"); 
							break;

					case REQ_CMDOUT : 
							while (((status_read = STATUS) & STAT_BSY) && ((status_read & REQ_MASK) ==
								REQ_CMDOUT))
								DATA = *(unsigned char *) cmnd ++;
						break;
	
					case REQ_STATIN : 
						status = DATA;
						break;
				
					case REQ_MSGOUT : 
						DATA = MESSAGE_REJECT;
						break;
					
					case REQ_MSGIN : 
						if ((message = DATA) == COMMAND_COMPLETE)
							done=1;
						
						break;

					default : printk("UNKNOWN PHASE"); st0x_aborted = DID_ERROR; 
					}	
				}

		}

#if (DEBUG & (PHASE_DATAIN | PHASE_DATAOUT | PHASE_EXIT))
	printk("Transfered %d bytes, allowed %d additional bytes\n", (bufflen - len), len);
#endif

#if (DEBUG & PHASE_EXIT)
		printk("Buffer : \n");
		for (i = 0; i < 20; ++i) 
			printk ("%02x  ", ((unsigned char *) buff)[i]);
		printk("\n");
		printk("Status = %02x, message = %02x\n", status, message);
#endif

	
		if (st0x_aborted)
			{
			if (STATUS & STAT_BSY)
				{
				seagate_st0x_reset();
				st0x_aborted = DID_RESET;
				}
			abort_confirm = 1;
			}	
			
		CONTROL = BASE_CMD;

#if (DEBUG & PHASE_EXIT)
	__asm__("
mov 4(%%ebp), %%eax
":"=a" (retaddr):);

	printk("Exiting seagate_st0x_command() - return address is %08x \n", retaddr);
	if (retaddr != realretaddr)
		panic ("Corrupted stack : return address on entry != return address on exit.\n");
	
#endif

		return retcode (st0x_aborted);
	}

int seagate_st0x_abort (int code)
	{
	if (code)
		st0x_aborted = code;
	else
		st0x_aborted = DID_ABORT;

		return 0;
	}

/*
	the seagate_st0x_reset function resets the SCSI bus
*/
	
int seagate_st0x_reset (void)
	{
	unsigned clock;
	/*
		No timeouts - this command is going to fail because 
		it was reset.
	*/

#ifdef DEBUG
	printk("In seagate_st0x_reset()\n");
#endif


	/* assert  RESET signal on SCSI bus.  */
		
	CONTROL = BASE_CMD  | CMD_RST;
	clock=jiffies+2;

	
	/* Wait.  */
	
	while (jiffies < clock);

	CONTROL = BASE_CMD;
	
	st0x_aborted = DID_RESET;

#ifdef DEBUG
	printk("SCSI bus reset.\n");
#endif
	return 0;
	}
#endif	
