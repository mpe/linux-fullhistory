/* $Id: eicon_pci.c,v 1.10 1999/08/22 20:26:49 calle Exp $
 *
 * ISDN low-level module for Eicon.Diehl active ISDN-Cards.
 * Hardware-specific code for PCI cards.
 *
 * Copyright 1998,99 by Armin Schindler (mac@melware.de)
 * Copyright 1999    Cytronics & Melware (info@melware.de)
 *
 * Thanks to	Eicon Technology Diehl GmbH & Co. oHG for 
 *		documents, informations and hardware. 
 *
 *		Deutsche Telekom AG for S2M support.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: eicon_pci.c,v $
 * Revision 1.10  1999/08/22 20:26:49  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.9  1999/08/11 21:01:11  keil
 * new PCI codefix
 *
 * Revision 1.8  1999/08/10 16:02:20  calle
 * struct pci_dev changed in 2.3.13. Made the necessary changes.
 *
 * Revision 1.7  1999/06/09 19:31:29  armin
 * Wrong PLX size for request_region() corrected.
 * Added first MCA code from Erik Weber.
 *
 * Revision 1.6  1999/04/01 12:48:37  armin
 * Changed some log outputs.
 *
 * Revision 1.5  1999/03/29 11:19:49  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.4  1999/03/02 12:37:48  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.3  1999/01/24 20:14:24  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.2  1999/01/10 18:46:06  armin
 * Bug with wrong values in HLC fixed.
 * Bytes to send are counted and limited now.
 *
 * Revision 1.1  1999/01/01 18:09:45  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#include <linux/config.h>
#include <linux/pci.h>

#include "eicon.h"
#include "eicon_pci.h"


char *eicon_pci_revision = "$Revision: 1.10 $";

#if CONFIG_PCI	         /* intire stuff is only for PCI */

#undef EICON_PCI_DEBUG 

int eicon_pci_find_card(char *ID)
{
  if (pci_present()) { 
    struct pci_dev *pdev = NULL;  
    int pci_nextindex=0, pci_cards=0, pci_akt=0; 
    int pci_type = PCI_MAESTRA;
    int NoMorePCICards = FALSE;
    char *ram, *reg, *cfg;	
    unsigned int pram=0, preg=0, pcfg=0;
    char did[12];
    eicon_pci_card *aparms;

   if (!(aparms = (eicon_pci_card *) kmalloc(sizeof(eicon_pci_card), GFP_KERNEL))) {
                  printk(KERN_WARNING
                      "eicon_pci: Could not allocate card-struct.\n");
                  return 0;
   }

  for (pci_cards = 0; pci_cards < 0x0f; pci_cards++)
  {
  do {
      if ((pdev = pci_find_device(PCI_VENDOR_EICON,          
                                  pci_type,                  
                                  pdev)))                    
	{
              pci_nextindex++;
              break;
	}
	else {
              pci_nextindex = 0;
              switch (pci_type) /* switch to next card type */
               {
               case PCI_MAESTRA:
                 pci_type = PCI_MAESTRAQ; break;
               case PCI_MAESTRAQ:
                 pci_type = PCI_MAESTRAQ_U; break;
               case PCI_MAESTRAQ_U:
                 pci_type = PCI_MAESTRAP; break;
               default:
               case PCI_MAESTRAP:
                 NoMorePCICards = TRUE;
               }
	}
     }
     while (!NoMorePCICards);
     if (NoMorePCICards)
        {
           if (pci_cards < 1) {
           printk(KERN_INFO "Eicon: No supported PCI cards found.\n");
	   kfree(aparms);	
           return 0;
           }
           else
           {
           printk(KERN_INFO "Eicon: %d PCI card%s registered.\n",
			pci_cards, (pci_cards > 1) ? "s":"");
	   kfree(aparms);	
           return (pci_cards);
           }
        }

   pci_akt = 0;
   switch(pci_type)
   {
    case PCI_MAESTRA:
         printk(KERN_INFO "Eicon: DIVA Server BRI/PCI detected !\n");
          aparms->type = EICON_CTYPE_MAESTRA;

          aparms->irq = pdev->irq;
          preg = get_pcibase(pdev, 2) & 0xfffffffc;
          pcfg = get_pcibase(pdev, 1) & 0xffffff80;

#ifdef EICON_PCI_DEBUG
          printk(KERN_DEBUG "eicon_pci: irq=%d\n", aparms->irq);
          printk(KERN_DEBUG "eicon_pci: reg=0x%x\n", preg);
          printk(KERN_DEBUG "eicon_pci: cfg=0x%x\n", pcfg);
#endif
	 pci_akt = 1;
         break;

    case PCI_MAESTRAQ:
    case PCI_MAESTRAQ_U:
         printk(KERN_ERR "Eicon: DIVA Server 4BRI/PCI detected but not supported !\n");
         pci_cards--;
	 pci_akt = 0;
         break;

    case PCI_MAESTRAP:
         printk(KERN_INFO "Eicon: DIVA Server PRI/PCI detected !\n");
          aparms->type = EICON_CTYPE_MAESTRAP; /*includes 9M,30M*/
          aparms->irq = pdev->irq;
          pram = get_pcibase(pdev, 0) & 0xfffff000;
          preg = get_pcibase(pdev, 2) & 0xfffff000;
          pcfg = get_pcibase(pdev, 4) & 0xfffff000;

#ifdef EICON_PCI_DEBUG
          printk(KERN_DEBUG "eicon_pci: irq=%d\n", aparms->irq);
          printk(KERN_DEBUG "eicon_pci: ram=0x%x\n",
               (pram));
          printk(KERN_DEBUG "eicon_pci: reg=0x%x\n",
               (preg));
          printk(KERN_DEBUG "eicon_pci: cfg=0x%x\n",
               (pcfg));
#endif
	  pci_akt = 1;
	  break;	
    default:
         printk(KERN_ERR "eicon_pci: Unknown PCI card detected !\n");
         pci_cards--;
	 pci_akt = 0;
	 break;
   }

	if (pci_akt) {
		/* remapping memory */
		switch(pci_type)
		{
    		case PCI_MAESTRA:
			aparms->PCIreg = (unsigned int) preg;
			aparms->PCIcfg = (unsigned int) pcfg;
			if (check_region((aparms->PCIreg), 0x20)) {
				printk(KERN_WARNING "eicon_pci: reg port already in use !\n");
				aparms->PCIreg = 0;
				break;	
			} else {
				request_region(aparms->PCIreg, 0x20, "eicon reg");
			}
			if (check_region((aparms->PCIcfg), 0x80)) {
				printk(KERN_WARNING "eicon_pci: cfg port already in use !\n");
				aparms->PCIcfg = 0;
				release_region(aparms->PCIreg, 0x20);
				break;	
			} else {
				request_region(aparms->PCIcfg, 0x80, "eicon cfg");
			}
			break;
    		case PCI_MAESTRAQ:
		case PCI_MAESTRAQ_U:
		case PCI_MAESTRAP:
			aparms->shmem = (eicon_pci_shmem *) ioremap(pram, 0x10000);
			ram = (u8 *) ((u32)aparms->shmem + MP_SHARED_RAM_OFFSET);
			reg =  ioremap(preg, 0x4000);
			cfg =  ioremap(pcfg, 0x1000);	
			aparms->PCIram = (unsigned int) ram;
			aparms->PCIreg = (unsigned int) reg;
			aparms->PCIcfg = (unsigned int) cfg;
			break;
		 }
		if ((!aparms->PCIreg) || (!aparms->PCIcfg)) {
			printk(KERN_ERR "eicon_pci: Card could not be added !\n");
			pci_cards--;
		} else {
			aparms->mvalid = 1;
	
			sprintf(did, "%s%d", (strlen(ID) < 1) ? "eicon":ID, pci_cards);

			printk(KERN_INFO "%s: DriverID: '%s'\n",eicon_ctype_name[aparms->type] , did);

			if (!(eicon_addcard(aparms->type, (int) aparms, aparms->irq, did))) {
				printk(KERN_ERR "eicon_pci: Card could not be added !\n");
				pci_cards--;
			}
		}
	}

  }
 } else
	printk(KERN_ERR "eicon_pci: Kernel compiled with PCI but no PCI-bios found !\n");
 return 0;
}

/*
 * Checks protocol file id for "F#xxxx" string fragment to
 * extract the features, supported by this protocol version.
 * binary representation of the feature string value is returned
 * in *value. The function returns 0 if feature string was not
 * found or has a wrong format, else 1.
 */
static int GetProtFeatureValue(char *sw_id, int *value)
{
  __u8 i, offset;

  while (*sw_id)
  {
    if ((sw_id[0] == 'F') && (sw_id[1] == '#'))
    {
      sw_id = &sw_id[2];
      for (i=0, *value=0; i<4; i++, sw_id++)
      {
        if ((*sw_id >= '0') && (*sw_id <= '9'))
        {
          offset = '0';
        }
        else if ((*sw_id >= 'A') && (*sw_id <= 'F'))
        {
          offset = 'A' + 10;
        }
        else if ((*sw_id >= 'a') && (*sw_id <= 'f'))
        {
          offset = 'a' + 10;
        }
        else
        {
          return 0;
        }
        *value |= (*sw_id - offset) << (4*(3-i));
      }
      return 1;
    }
    else
    {
      sw_id++;
    }
  }
  return 0;
}


void
eicon_pci_printpar(eicon_pci_card *card) {
        switch (card->type) {
                case EICON_CTYPE_MAESTRA:
			printk(KERN_INFO "%s at 0x%x / 0x%x, irq %d\n",
				eicon_ctype_name[card->type],
				(unsigned int)card->PCIreg,
				(unsigned int)card->PCIcfg,
				card->irq); 
			break;
                case EICON_CTYPE_MAESTRAQ:
                case EICON_CTYPE_MAESTRAQ_U:
                case EICON_CTYPE_MAESTRAP:
			printk(KERN_INFO "%s at 0x%x, irq %d\n",
				eicon_ctype_name[card->type],
				(unsigned int)card->shmem,
				card->irq); 
#ifdef EICON_PCI_DEBUG
		        printk(KERN_INFO "eicon_pci: remapped ram= 0x%x\n",(unsigned int)card->PCIram);
		        printk(KERN_INFO "eicon_pci: remapped reg= 0x%x\n",(unsigned int)card->PCIreg);
		        printk(KERN_INFO "eicon_pci: remapped cfg= 0x%x\n",(unsigned int)card->PCIcfg); 
#endif
			break;
	}
}


static void
eicon_pci_release_shmem(eicon_pci_card *card) {
	if (!card->master)
		return;
	if (card->mvalid) {
        	switch (card->type) {
                	case EICON_CTYPE_MAESTRA:
			        /* reset board */
				outb(0, card->PCIcfg + 0x4c);	/* disable interrupts from PLX */
				outb(0, card->PCIreg + M_RESET);
				SLEEP(20);
				outb(0, card->PCIreg + M_ADDRH);
				outw(0, card->PCIreg + M_ADDR);
				outw(0, card->PCIreg + M_DATA);

				release_region(card->PCIreg, 0x20);
				release_region(card->PCIcfg, 0x80);
				break;
                	case EICON_CTYPE_MAESTRAQ:
	                case EICON_CTYPE_MAESTRAQ_U:
	                case EICON_CTYPE_MAESTRAP:
			        /* reset board */
		        	writeb(_MP_RISC_RESET | _MP_LED1 | _MP_LED2, card->PCIreg + MP_RESET);
			        SLEEP(20);
			        writeb(0, card->PCIreg + MP_RESET);
			        SLEEP(20);

				iounmap((void *)card->shmem);
				iounmap((void *)card->PCIreg);
				iounmap((void *)card->PCIcfg);
				break;
		}
	}
	card->mvalid = 0;
}

static void
eicon_pci_release_irq(eicon_pci_card *card) {
	if (!card->master)
		return;
	if (card->ivalid)
		free_irq(card->irq, card);
	card->ivalid = 0;
}

void
eicon_pci_release(eicon_pci_card *card) {
        eicon_pci_release_irq(card);
        eicon_pci_release_shmem(card);
}

/*
 * Upload buffer content to adapters shared memory
 * on verify error, 1 is returned and a message is printed on screen
 * else 0 is returned
 * Can serve IO-Type and Memory type adapters
 */
int eicon_upload(t_dsp_download_space   *p_para,
            __u16                 length,   /* byte count */
            __u8                  *buffer,
            int                   verify)
{
  __u32               i, dwdata = 0, val = 0, timeout;
  __u16               data;
  eicon_pci_boot *boot = 0;

  switch (p_para->type) /* actions depend on type of union */
  {
    case DL_PARA_IO_TYPE:
      for (i=0; i<length; i+=2)
      {
	outb ((u8) ((p_para->dat.io.r3addr + i) >> 16), p_para->dat.io.ioADDRH);
	outw ((u16) (p_para->dat.io.r3addr + i), p_para->dat.io.ioADDR); 
	/* outw (((u16 *)code)[i >> 1], p_para->dat.io.ioDATA); */
	outw (*(u16 *)&buffer[i], p_para->dat.io.ioDATA); 
      }
      if (verify) /* check written block */
      {
        for (i=0; i<length; i+=2)
        {
	  outb ((u8) ((p_para->dat.io.r3addr + i) >> 16), p_para->dat.io.ioADDRH);
          outw ((u16) (p_para->dat.io.r3addr + i), p_para->dat.io.ioADDR); 
          data = inw(p_para->dat.io.ioDATA);
          if (data != *(u16 *)&buffer[i])
          {
            p_para->dat.io.r3addr  += i;
            p_para->dat.io.BadData  = data;
            p_para->dat.io.GoodData = *(u16 *)&buffer[i];
            return 1;
          }
        }
      }
      break;

    case DL_PARA_MEM_TYPE:
      boot = p_para->dat.mem.boot;
      writel(p_para->dat.mem.r3addr, &boot->addr);
      for (i=0; i<length; i+=4)
      {
        writel(((u32 *)buffer)[i >> 2], &boot->data[i]);
      }
      if (verify) /* check written block */
      {
        for (i=0; i<length; i+=4)
        {
          dwdata = readl(&boot->data[i]);
          if (((u32 *)buffer)[i >> 2] != dwdata)
          {
            p_para->dat.mem.r3addr  += i;
            p_para->dat.mem.BadData  = dwdata;
            p_para->dat.mem.GoodData = ((u32 *)buffer)[i >> 2];
            return 1;
          }
        }
      }
      writel(((length + 3) / 4), &boot->len);  /* len in dwords */
      writel(2, &boot->cmd);

	timeout = jiffies + 20;
	while (timeout > jiffies) {
		val = readl(&boot->cmd);
		if (!val) break;
		SLEEP(2);
	}
	if (val)
         {
		p_para->dat.mem.timeout = 1;
		return 1;
	 }
      break;
  }
  return 0;
}


/* show header information of code file */
static
int eicon_pci_print_hdr(unsigned char *code, int offset)
{
  unsigned char hdr[80];
  int i, fvalue = 0;

  i = 0;
  while ((i < (sizeof(hdr) -1))
          && (code[offset + i] != '\0')
          && (code[offset + i] != '\r')
          && (code[offset + i] != '\n'))
   {
     hdr[i] = code[offset + i];
     i++;
   }
   hdr[i] = '\0';
   printk(KERN_DEBUG "Eicon: loading %s\n", hdr);
   if (GetProtFeatureValue(hdr, &fvalue)) return(fvalue);
    else return(0);
}


/*
 * Configure a card, download code into BRI card,
 * check if we get interrupts and return 0 on succes.
 * Return -ERRNO on failure.
 */
int
eicon_pci_load_bri(eicon_pci_card *card, eicon_pci_codebuf *cb) {
        int               i,j;
        int               timeout;
	unsigned int	  offset, offp=0, size, length;
	int		  signature = 0;
	int		  FeatureValue = 0;
        eicon_pci_codebuf cbuf;
	t_dsp_download_space dl_para;
	t_dsp_download_desc  dsp_download_table;
        unsigned char     *code;
	unsigned int	  reg;
	unsigned int	  cfg;

        if (copy_from_user(&cbuf, cb, sizeof(eicon_pci_codebuf)))
                return -EFAULT;

	reg = card->PCIreg;
	cfg = card->PCIcfg;

	/* reset board */
	outb(0, reg + M_RESET);
	SLEEP(10);
	outb(0, reg + M_ADDRH);
	outw(0, reg + M_ADDR);
	outw(0, reg + M_DATA);

#ifdef EICON_PCI_DEBUG
	 printk(KERN_DEBUG "eicon_pci: reset card\n");
#endif

	/* clear shared memory */
	outb(0xff, reg + M_ADDRH);
	outw(0, reg + M_ADDR);
	for(i = 0; i < 0xffff; i++) outw(0, reg + M_DATA);
	SLEEP(10);

#ifdef EICON_PCI_DEBUG
	 printk(KERN_DEBUG "eicon_pci: clear shared memory\n");
#endif

	/* download protocol and dsp file */

#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: downloading firmware...\n");
#endif

       	/* Allocate code-buffer */
       	if (!(code = kmalloc(400, GFP_KERNEL))) {
                printk(KERN_WARNING "eicon_pci_boot: Couldn't allocate code buffer\n");
       	        return -ENOMEM;
        }

	/* prepare protocol upload */
	dl_para.type		= DL_PARA_IO_TYPE;
	dl_para.dat.io.ioADDR	= reg + M_ADDR;
	dl_para.dat.io.ioADDRH	= reg + M_ADDRH;
	dl_para.dat.io.ioDATA	= reg + M_DATA;

	for (j = 0; j <= cbuf.dsp_code_num; j++) 
	 {	
	   if (j == 0)  size = cbuf.protocol_len;
	           else size = cbuf.dsp_code_len[j];

        	offset = 0;

		if (j == 0) dl_para.dat.io.r3addr = 0;
		if (j == 1) dl_para.dat.io.r3addr = M_DSP_CODE_BASE +
					((sizeof(__u32) + (sizeof(dsp_download_table) * 35) + 3) &0xfffffffc);
		if (j == 2) dl_para.dat.io.r3addr = M_DSP_CODE_BASE;
		if (j == 3) dl_para.dat.io.r3addr = M_DSP_CODE_BASE + sizeof(__u32);

           do  /* download block of up to 400 bytes */
            {
              length = ((size - offset) >= 400) ? 400 : (size - offset);

        	if (copy_from_user(code, (&cb->code) + offp + offset, length)) {
                	kfree(code);
	                return -EFAULT;
        	}

		if ((offset == 0) && (j < 2)) {
		       	FeatureValue = eicon_pci_print_hdr(code, j ? 0x00 : 0x80); 
#ifdef EICON_PCI_DEBUG
	if (FeatureValue) printk(KERN_DEBUG "eicon_pci: Feature Value : 0x%04x.\n", FeatureValue);
#endif
			if ((j==0) && (!(FeatureValue & PROTCAP_TELINDUS))) {
                  		printk(KERN_ERR "eicon_pci: Protocol Code cannot handle Telindus\n");
				kfree(code);
		                return -EFAULT;
			}
                	((eicon_card *)card->card)->Feature = FeatureValue;
		}

		if (eicon_upload(&dl_para, length, code, 1))
		{
                  printk(KERN_ERR "eicon_pci: code block check failed at 0x%x !\n",dl_para.dat.io.r3addr);
		  kfree(code);
                  return -EIO;
		}
              /* move onto next block */
              offset += length;
	      dl_para.dat.io.r3addr += length;
            } while (offset < size);

#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "Eicon: %d bytes loaded.\n", offset);
#endif
	offp += size;
	}
	kfree(code);	

	/* clear signature */
	outb(0xff, reg + M_ADDRH);
	outw(0x1e, reg + M_ADDR);
	outw(0, reg + M_DATA);

#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: copy configuration data into shared memory...\n");
#endif
	/* copy configuration data into shared memory */
	outw(8, reg + M_ADDR); outb(cbuf.tei, reg + M_DATA);
	outw(9, reg + M_ADDR); outb(cbuf.nt2, reg + M_DATA);
	outw(10,reg + M_ADDR); outb(0, reg + M_DATA);
	outw(11,reg + M_ADDR); outb(cbuf.WatchDog, reg + M_DATA);
	outw(12,reg + M_ADDR); outb(cbuf.Permanent, reg + M_DATA);
	outw(13,reg + M_ADDR); outb(0, reg + M_DATA);                 /* XInterface */
	outw(14,reg + M_ADDR); outb(cbuf.StableL2, reg + M_DATA);
	outw(15,reg + M_ADDR); outb(cbuf.NoOrderCheck, reg + M_DATA);
	outw(16,reg + M_ADDR); outb(0, reg + M_DATA);                 /* HandsetType */
	outw(17,reg + M_ADDR); outb(0, reg + M_DATA);                 /* SigFlags */
	outw(18,reg + M_ADDR); outb(cbuf.LowChannel, reg + M_DATA);
	outw(19,reg + M_ADDR); outb(cbuf.ProtVersion, reg + M_DATA);
	outw(20,reg + M_ADDR); outb(cbuf.Crc4, reg + M_DATA);
	outw(21,reg + M_ADDR); outb((cbuf.Loopback) ? 2:0, reg + M_DATA);

	for (i=0;i<32;i++)
	{
		outw( 32+i, reg + M_ADDR); outb(cbuf.l[0].oad[i], reg + M_DATA);
		outw( 64+i, reg + M_ADDR); outb(cbuf.l[0].osa[i], reg + M_DATA);
		outw( 96+i, reg + M_ADDR); outb(cbuf.l[0].spid[i], reg + M_DATA);
		outw(128+i, reg + M_ADDR); outb(cbuf.l[1].oad[i], reg + M_DATA);
		outw(160+i, reg + M_ADDR); outb(cbuf.l[1].osa[i], reg + M_DATA);
		outw(192+i, reg + M_ADDR); outb(cbuf.l[1].spid[i], reg + M_DATA);
	}

#ifdef EICON_PCI_DEBUG
           printk(KERN_ERR "eicon_pci: starting CPU...\n");
#endif
	/* let the CPU run */
	outw(0x08, reg + M_RESET);

        timeout = jiffies + (5*HZ);
        while (timeout > jiffies) {
	   outw(0x1e, reg + M_ADDR);	
           signature = inw(reg + M_DATA);
           if (signature == DIVAS_SIGNATURE) break;
           SLEEP(2);
         }
        if (signature != DIVAS_SIGNATURE)
         {
#ifdef EICON_PCI_DEBUG
           printk(KERN_ERR "eicon_pci: signature 0x%x expected 0x%x\n",signature,DIVAS_SIGNATURE);
#endif
           printk(KERN_ERR "eicon_pci: Timeout, protocol code not running !\n");
           return -EIO; 
         }
#ifdef EICON_PCI_DEBUG
        printk(KERN_DEBUG "eicon_pci: Protocol code running, signature OK\n");
#endif

        /* get serial number and number of channels supported by card */
	outb(0xff, reg + M_ADDRH);
	outw(0x3f6, reg + M_ADDR);
        card->channels = inw(reg + M_DATA);
        card->serial = (u32)inw(cfg + 0x22) << 16 | (u32)inw(cfg + 0x26);
        printk(KERN_INFO "Eicon: Supported channels : %d\n", card->channels);
        printk(KERN_INFO "Eicon: Card serial no. = %lu\n", card->serial);

        /* test interrupt */
        card->irqprobe = 1;

        if (!card->ivalid) {
                if (request_irq(card->irq, &eicon_irq, 0, "Eicon PCI ISDN", card->card))
                 {
                  printk(KERN_ERR "eicon_pci: Couldn't request irq %d\n", card->irq);
                  return -EIO;
                 }
        }
        card->ivalid = 1;

#ifdef EICON_PCI_DEBUG
        printk(KERN_DEBUG "eicon_pci: testing interrupt\n");
#endif
        /* Trigger an interrupt and check if it is delivered */
        outb(0x41, cfg + 0x4c);		/* enable PLX for interrupts */
	outb(0x89, reg + M_RESET);	/* place int request */

        timeout = jiffies + 20;
        while (timeout > jiffies) {
          if (card->irqprobe != 1) break;
          SLEEP(5);
         }
        if (card->irqprobe == 1) {
           free_irq(card->irq, card); 
           card->ivalid = 0; 
           printk(KERN_ERR "eicon_pci: Getting no interrupts !\n");
           return -EIO;
         }

   /* initializing some variables */
   ((eicon_card *)card->card)->ReadyInt = 0;
   for(j=0; j<256; j++) ((eicon_card *)card->card)->IdTable[j] = NULL;
   for(j=0; j< (card->channels + 1); j++) {
                ((eicon_card *)card->card)->bch[j].e.busy = 0;
                ((eicon_card *)card->card)->bch[j].e.D3Id = 0;
                ((eicon_card *)card->card)->bch[j].e.B2Id = 0;
                ((eicon_card *)card->card)->bch[j].e.ref = 0;
                ((eicon_card *)card->card)->bch[j].e.Req = 0;
                ((eicon_card *)card->card)->bch[j].e.complete = 1;
                ((eicon_card *)card->card)->bch[j].fsm_state = EICON_STATE_NULL;
   }

   printk(KERN_INFO "Eicon: Card successfully started\n");

 return 0;
}


/*
 * Configure a card, download code into PRI card,
 * check if we get interrupts and return 0 on succes.
 * Return -ERRNO on failure.
 */
int
eicon_pci_load_pri(eicon_pci_card *card, eicon_pci_codebuf *cb) {
        eicon_pci_boot    *boot;
	eicon_pr_ram  *prram;
        int               i,j;
        int               timeout;
	int		  FeatureValue = 0;
	unsigned int	  offset, offp=0, size, length;
	unsigned long int signature = 0;
	t_dsp_download_space dl_para;
	t_dsp_download_desc  dsp_download_table;
        eicon_pci_codebuf cbuf;
        unsigned char     *code;
	unsigned char	  req_int;
    	char *ram, *reg, *cfg;	

        if (copy_from_user(&cbuf, cb, sizeof(eicon_pci_codebuf)))
                return -EFAULT;

        boot = &card->shmem->boot;
	ram = (char *)card->PCIram;
	reg = (char *)card->PCIreg;
	cfg = (char *)card->PCIcfg;
	prram = (eicon_pr_ram *)ram;

	/* reset board */
	writeb(_MP_RISC_RESET | _MP_LED1 | _MP_LED2, card->PCIreg + MP_RESET);
	SLEEP(20);
	writeb(0, card->PCIreg + MP_RESET);
	SLEEP(20);

	/* set command count to 0 */
	writel(0, &boot->reserved); 

	/* check if CPU increments the life word */
        i = readw(&boot->live);
        SLEEP(20);
        if (i == readw(&boot->live)) {
           printk(KERN_ERR "eicon_pci: card is reset, but CPU not running !\n");
           return -EIO;
         }
#ifdef EICON_PCI_DEBUG
	 printk(KERN_DEBUG "eicon_pci: reset card OK (CPU running)\n");
#endif

	/* download firmware : DSP and Protocol */
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: downloading firmware...\n");
#endif

       	/* Allocate code-buffer */
       	if (!(code = kmalloc(400, GFP_KERNEL))) {
                printk(KERN_WARNING "eicon_pci_boot: Couldn't allocate code buffer\n");
       	        return -ENOMEM;
        }

	/* prepare protocol upload */
	dl_para.type		= DL_PARA_MEM_TYPE;
	dl_para.dat.mem.boot	= boot;

        for (j = 0; j <= cbuf.dsp_code_num; j++)
         {
	   if (j==0) size = cbuf.protocol_len;
		else size = cbuf.dsp_code_len[j];	

           if (j==1) writel(MP_DSP_ADDR, &boot->addr); /* DSP code entry point */

		if (j == 0) dl_para.dat.io.r3addr = MP_PROTOCOL_ADDR;
		if (j == 1) dl_para.dat.io.r3addr = MP_DSP_CODE_BASE +
					((sizeof(__u32) + (sizeof(dsp_download_table) * 35) + 3) &0xfffffffc);
		if (j == 2) dl_para.dat.io.r3addr = MP_DSP_CODE_BASE;
		if (j == 3) dl_para.dat.io.r3addr = MP_DSP_CODE_BASE + sizeof(__u32);

           offset = 0;
           do  /* download block of up to 400 bytes */
            {
              length = ((size - offset) >= 400) ? 400 : (size - offset);

        	if (copy_from_user(code, (&cb->code) + offp + offset, length)) {
                	kfree(code);
	                return -EFAULT;
        	}

		if ((offset == 0) && (j < 2)) {
	           	FeatureValue = eicon_pci_print_hdr(code, j ? 0x00 : 0x80); 
#ifdef EICON_PCI_DEBUG
	if (FeatureValue) printk(KERN_DEBUG "eicon_pci: Feature Value : 0x%x.\n", FeatureValue);
#endif
			if ((j==0) && (!(FeatureValue & PROTCAP_TELINDUS))) {
                  		printk(KERN_ERR "eicon_pci: Protocol Code cannot handle Telindus\n");
				kfree(code);
		                return -EFAULT;
			}
                	((eicon_card *)card->card)->Feature = FeatureValue;
		}

		if (eicon_upload(&dl_para, length, code, 1))
		{
		  if (dl_para.dat.mem.timeout == 0)
	                  printk(KERN_ERR "eicon_pci: code block check failed at 0x%x !\n",dl_para.dat.io.r3addr);
			else
			  printk(KERN_ERR "eicon_pci: timeout, no ACK to load !\n");
		  kfree(code);
                  return -EIO;
		}

              /* move onto next block */
              offset += length;
	      dl_para.dat.mem.r3addr += length;
            } while (offset < size);
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: %d bytes loaded.\n", offset);
#endif
	 offp += size;
         }
	 kfree(code);	

	/* initialize the adapter data structure */
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: copy configuration data into shared memory...\n");
#endif
        /* clear out config space */
        for (i = 0; i < 256; i++) writeb(0, &ram[i]);

        /* copy configuration down to the card */
        writeb(cbuf.tei, &ram[8]);
        writeb(cbuf.nt2, &ram[9]);
        writeb(0, &ram[10]);
        writeb(cbuf.WatchDog, &ram[11]);
        writeb(cbuf.Permanent, &ram[12]);
        writeb(cbuf.XInterface, &ram[13]);
        writeb(cbuf.StableL2, &ram[14]);
        writeb(cbuf.NoOrderCheck, &ram[15]);
        writeb(cbuf.HandsetType, &ram[16]);
        writeb(0, &ram[17]);
        writeb(cbuf.LowChannel, &ram[18]);
        writeb(cbuf.ProtVersion, &ram[19]);
        writeb(cbuf.Crc4, &ram[20]);
        for (i = 0; i < 32; i++)
         {
           writeb(cbuf.l[0].oad[i], &ram[32 + i]);
           writeb(cbuf.l[0].osa[i], &ram[64 + i]);
           writeb(cbuf.l[0].spid[i], &ram[96 + i]);
           writeb(cbuf.l[1].oad[i], &ram[128 + i]);
           writeb(cbuf.l[1].osa[i], &ram[160 + i]);
           writeb(cbuf.l[1].spid[i], &ram[192 + i]);
         }
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: configured card OK\n");
#endif

	/* start adapter */
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: tell card to start...\n");
#endif
        writel(MP_PROTOCOL_ADDR, &boot->addr); /* RISC code entry point */
        writel(3, &boot->cmd); /* DIVAS_START_CMD */

        /* wait till card ACKs */
        timeout = jiffies + (5*HZ);
        while (timeout > jiffies) {
           signature = readl(&boot->signature);
           if ((signature >> 16) == DIVAS_SIGNATURE) break;
           SLEEP(2);
         }
        if ((signature >> 16) != DIVAS_SIGNATURE)
         {
#ifdef EICON_PCI_DEBUG
           printk(KERN_ERR "eicon_pci: signature 0x%lx expected 0x%x\n",(signature >> 16),DIVAS_SIGNATURE);
#endif
           printk(KERN_ERR "eicon_pci: timeout, protocol code not running !\n");
           return -EIO;
         }
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: Protocol code running, signature OK\n");
#endif

	/* get serial number and number of channels supported by card */
        card->channels = readb(&ram[0x3f6]);
        card->serial = readl(&ram[0x3f0]);
        printk(KERN_INFO "Eicon: Supported channels : %d\n", card->channels);
        printk(KERN_INFO "Eicon: Card serial no. = %lu\n", card->serial);

	/* test interrupt */
	readb(&ram[0x3fe]);
        writeb(0, &ram[0x3fe]); /* reset any pending interrupt */
	readb(&ram[0x3fe]);

        writew(MP_IRQ_RESET_VAL, &cfg[MP_IRQ_RESET]);
        writew(0, &cfg[MP_IRQ_RESET + 2]);

        card->irqprobe = 1;

	if (!card->ivalid) {
	        if (request_irq(card->irq, &eicon_irq, 0, "Eicon PCI ISDN", card->card)) 
        	 {
	          printk(KERN_ERR "eicon_pci: Couldn't request irq %d\n", card->irq);
        	  return -EIO;
	         }
	}
	card->ivalid = 1;

        req_int = readb(&prram->ReadyInt);
#ifdef EICON_PCI_DEBUG
	printk(KERN_DEBUG "eicon_pci: testing interrupt\n");
#endif
        req_int++;
        /* Trigger an interrupt and check if it is delivered */
        writeb(req_int, &prram->ReadyInt);

        timeout = jiffies + 20;
        while (timeout > jiffies) {
          if (card->irqprobe != 1) break;
          SLEEP(2);
         }
        if (card->irqprobe == 1) {
           free_irq(card->irq, card);
	   card->ivalid = 0;
           printk(KERN_ERR "eicon_pci: Getting no interrupts !\n");
           return -EIO;
         }

   /* initializing some variables */
   ((eicon_card *)card->card)->ReadyInt = 0;
   for(j=0; j<256; j++) ((eicon_card *)card->card)->IdTable[j] = NULL;
   for(j=0; j< (card->channels + 1); j++) {
		((eicon_card *)card->card)->bch[j].e.busy = 0;
		((eicon_card *)card->card)->bch[j].e.D3Id = 0;
		((eicon_card *)card->card)->bch[j].e.B2Id = 0;
		((eicon_card *)card->card)->bch[j].e.ref = 0;
		((eicon_card *)card->card)->bch[j].e.Req = 0;
                ((eicon_card *)card->card)->bch[j].e.complete = 1;
                ((eicon_card *)card->card)->bch[j].fsm_state = EICON_STATE_NULL;
   }

   printk(KERN_INFO "Eicon: Card successfully started\n");

 return 0;
}

#endif	/* CONFIG_PCI */

