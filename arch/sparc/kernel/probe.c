/* probe.c: Preliminary device tree probing routines...

   Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
*/

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/vac-ops.h>
#include <asm/io.h>
#include <asm/vaddrs.h>
#include <asm/param.h>
#include <asm/clock.h>
#include <asm/system.h>

/* #define DEBUG_PROBING */

char promstr_buf[64];         /* overkill */
unsigned int promint_buf[1];

extern int prom_node_root;
extern int num_segmaps, num_contexts;

extern int node_get_sibling(int node);
extern int node_get_child(int node);
extern char* get_str_from_prom(int node, char* name, char* value);
extern unsigned int* get_int_from_prom(int node, char* name, unsigned int *value);

int first_descent;

/* Cpu-type information and manufacturer strings */


struct cpu_iu_info {
  int psr_impl;
  int psr_vers;
  char* cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
  int psr_impl;
  int fp_vers;
  char* fp_name;
};

struct cpu_fp_info linux_sparc_fpu[] = {
  { 0, 0, "Fujitsu MB86910 or Weitek WTL1164/5"},
  { 0, 1, "Fujitsu MB86911 or Weitek WTL1164/5"},
  { 0, 2, "LSI Logic L64802 or Texas Instruments ACT8847"},
  { 0, 3, "Weitek WTL3170/2"},
  { 0, 4, "Lsi Logic/Meiko L64804"},
  { 0, 5, "reserved"},
  { 0, 6, "reserved"},
  { 0, 7, "No FPU"},
  { 1, 0, "Lsi Logic L64812 or Texas Instruments ACT8847"},
  { 1, 1, "Lsi Logic L64814"},
  { 1, 2, "Texas Instruments TMS390-C602A"},
  { 1, 3, "Weitek WTL3171"},
  { 1, 4, "reserved"},
  { 1, 5, "reserved"},
  { 1, 6, "reserved"},
  { 1, 7, "No FPU"},
  { 2, 0, "BIT B5010 or B5110/20 or B5210"},
  { 2, 1, "reserved"},
  { 2, 2, "reserved"},
  { 2, 3, "reserved"},
  { 2, 4,  "reserved"},
  { 2, 5, "reserved"},
  { 2, 6, "reserved"},
  { 2, 7, "No FPU"},
  { 5, 0, "Matsushita MN10501"},
  { 5, 1, "reserved"},
  { 5, 2, "reserved"},
  { 5, 3, "reserved"},
  { 5, 4, "reserved"},
  { 5, 5, "reserved"},
  { 5, 6, "reserved"},
  { 5, 7, "No FPU"},
};

struct cpu_iu_info linux_sparc_chips[] = {
  { 0, 0, "Fujitsu Microelectronics, Inc. - MB86900/1A"},
  { 1, 0, "Cypress CY7C601"},
  { 1, 1, "LSI Logic Corporation - L64811"},
  { 1, 3, "Cypress CY7C611"},
  { 2, 0, "Bipolar Integrated Technology - B5010"},
  { 3, 0, "LSI Logic Corporation - unknown-type"},
  { 4, 0, "Texas Instruments, Inc. - unknown"},
  { 4, 1, "Texas Instruments, Inc. - Sparc Classic"},
  { 4, 2, "Texas Instruments, Inc. - unknown"},
  { 4, 3, "Texas Instruments, Inc. - unknown"},
  { 4, 4, "Texas Instruments, Inc. - unknown"},
  { 4, 5, "Texas Instruments, Inc. - unknown"},
  { 5, 0, "Matsushita - MN10501"},
  { 6, 0, "Philips Corporation - unknown"},
  { 7, 0, "Harvest VLSI Design Center, Inc. - unknown"},
  { 8, 0, "Systems and Processes Engineering Corporation (SPEC)"},
  { 9, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xa, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xb, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xc, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xd, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xe, 0, "UNKNOWN CPU-VENDOR/TYPE"},
  { 0xf, 0, "UNKNOWN CPU-VENDOR/TYPE"},
};

char *sparc_cpu_type = "cpu-oops";
char *sparc_fpu_type = "fpu-oops";

/* various Virtual Address Cache parameters we find at boot time... */

extern int vac_size, vac_linesize, vac_do_hw_vac_flushes;
extern int vac_entries_per_context, vac_entries_per_segment;
extern int vac_entries_per_page;

extern int find_vac_size(void);
extern int find_vac_linesize(void);
extern int find_vac_hwflushes(void);
extern void find_mmu_num_segmaps(void);
extern void find_mmu_num_contexts(void);

void
probe_cpu(void)
{
  register int psr_impl=0;
  register int psr_vers = 0;
  register int fpu_vers = 0;
  register int i = 0;
  unsigned int tmp_fsr;

  &tmp_fsr;   /* GCC grrr... */

  __asm__("rd %%psr, %0\n\t"
	  "mov %0, %1\n\t"
	  "srl %0, 28, %0\n\t"
	  "srl %1, 24, %1\n\t"
	  "and %0, 0xf, %0\n\t"
	  "and %1, 0xf, %1\n\t" :
	  "=r" (psr_impl),
	  "=r" (psr_vers) :
	  "0" (psr_impl),
	  "1" (psr_vers));


  __asm__("st %%fsr, %1\n\t"
	  "ld %1, %0\n\t"
	  "srl %0, 17, %0\n\t"
	  "and %0, 0x7, %0\n\t" :
	  "=r" (fpu_vers),
	  "=m" (tmp_fsr) :
	  "0" (fpu_vers),
	  "1" (tmp_fsr));

  printk("fpu_vers: %d ", fpu_vers);
  printk("psr_impl: %d ", psr_impl);
  printk("psr_vers: %d \n\n", psr_vers);

  for(i = 0; i<23; i++)
    {
      if(linux_sparc_chips[i].psr_impl == psr_impl)
	if(linux_sparc_chips[i].psr_vers == psr_vers)
	  {
	    sparc_cpu_type = linux_sparc_chips[i].cpu_name;
	    break;
	  }
    }

  if(i==23)
    {
      printk("No CPU type! You lose\n");
      printk("DEBUG: psr.impl = 0x%x   psr.vers = 0x%x\n", psr_impl, 
	     psr_vers);
      return;
    }

  for(i = 0; i<32; i++)
    {
      if(linux_sparc_fpu[i].psr_impl == psr_impl)
	if(linux_sparc_fpu[i].fp_vers == fpu_vers)
	  {
	    sparc_fpu_type = linux_sparc_fpu[i].fp_name;
	    break;
	  }
    }

  if(i == 32)
    {
      printk("No FPU type! You don't completely lose though...\n");
      printk("DEBUG: psr.impl = 0x%x  fsr.vers = 0x%x\n", psr_impl, fpu_vers);
      sparc_fpu_type = linux_sparc_fpu[31].fp_name;
    }

  printk("CPU: %s \n", sparc_cpu_type);
  printk("FPU: %s \n", sparc_fpu_type);

  return;
}

void
probe_vac(void)
{
  register unsigned int x,y;

#ifndef CONFIG_SRMMU
  vac_size = find_vac_size();
  vac_linesize = find_vac_linesize();
  vac_do_hw_vac_flushes = find_vac_hwflushes();

  /* Calculate various constants that make the cache-flushing code
   * mode speedy.
   */

  vac_entries_per_segment = vac_entries_per_context = vac_size >> 12;

  for(x=0,y=vac_linesize; ((1<<x)<y); x++);
  if((1<<x) != vac_linesize) printk("Warning BOGUS VAC linesize 0x%x",
				    vac_size);

  vac_entries_per_page = x;

  printk("Sparc VAC cache: Size=%d bytes  Line-Size=%d bytes ... ", vac_size,
	 vac_linesize);

  /* Here we want to 'invalidate' all the software VAC "tags"
   * just in case there is garbage in there. Then we enable it.
   */

  for(x=0x80000000, y=(x+vac_size); x<y; x+=vac_linesize)
    __asm__("sta %0, [%1] %2" : : "r" (0), "r" (x), "n" (0x2));

  x=enable_vac();
  printk("ENABLED\n");
#endif

  return;
}

void
probe_mmu(void)
{
  find_mmu_num_segmaps();
  find_mmu_num_contexts();

  printk("MMU segmaps: %d     MMU contexts: %d\n", num_segmaps, 
	 num_contexts);

  return;
}

void
probe_clock(int fchild)
{
  register int node, type;
  register char *node_str;

  /* This will basically traverse the node-tree of the prom to see
   * which timer chip is on this machine.
   */

  printk("Probing timer chip... ");

  type = 0;
  for(node = fchild ; ; )
    {
      node_str = get_str_from_prom(node, "model", promstr_buf);
      if(strcmp(node_str, "mk48t02") == 0)
	{
	  type = 2;
	  break;
	}

      if(strcmp(node_str, "mk48t08") == 0)
	{
	  type = 8;
	  break;
	}

      node = node_get_sibling(node);
      if(node == fchild)
	{
	  printk("Aieee, could not find timer chip type\n");
	  return;
	}
    }

  printk("Mostek %s\n", node_str);
  printk("At OBIO address: 0x%x Virtual address: 0x%x\n",
	 (unsigned int) TIMER_PHYSADDR, (unsigned int) TIMER_STRUCT);

  mapioaddr((unsigned long) TIMER_PHYSADDR,
	    (unsigned long) TIMER_STRUCT);

  TIMER_STRUCT->timer_limit14=(((1000000/HZ) << 10) | 0x80000000);

  return;
}


void
probe_esp(register int esp_node)
{
  register int nd;
  register char* lbuf;

  nd = node_get_child(esp_node);

  printk("\nProbing ESP:\n");
  lbuf = get_str_from_prom(nd, "name", promstr_buf);

  if(*get_int_from_prom(nd, "name", promint_buf) != 0)
  printk("Node: 0x%x Name: %s\n", nd, lbuf);

  while((nd = node_get_sibling(nd)) != 0) {
    lbuf = get_str_from_prom(nd, "name", promstr_buf);
    printk("Node: 0x%x Name: %s\n", nd, lbuf);
  }

  printk("\n");

  return;
}

void
probe_sbus(register int cpu_child_node)
{
  register int nd, savend;
  register char* lbuf;

  nd = cpu_child_node;

  lbuf = (char *) 0;

  while((nd = node_get_sibling(nd)) != 0) {
    lbuf = get_str_from_prom(nd, "name", promstr_buf);
    if(strcmp(lbuf, "sbus") == 0)
      break;
  };

  nd = node_get_child(nd);

  printk("Node: 0x%x Name: %s\n", nd,
	 get_str_from_prom(nd, "name", promstr_buf));

  if(strcmp(lbuf, "esp") == 0) {
    probe_esp(nd);
  };

  while((nd = node_get_sibling(nd)) != 0) {
    printk("Node: 0x%x Name: %s\n", nd,
	   lbuf = get_str_from_prom(nd, "name", promstr_buf));
    
    if(strcmp(lbuf, "esp") == 0) {
      savend = nd;
      probe_esp(nd);
      nd = savend;
    };
  };

  printk("\n");
  return;
}

extern unsigned long probe_memory(void);
extern struct sparc_phys_banks sp_banks[14];
unsigned int phys_bytes_of_ram, end_of_phys_memory;

void
probe_devices(void)
{
  register int nd, i;
  register char* str;

  nd = prom_node_root;

  printk("PROBING DEVICES:\n");

  str = get_str_from_prom(nd, "device_type", promstr_buf);
  if(strcmp(str, "cpu") == 0) {
    printk("Found CPU root prom device tree node.\n");
  } else {
    printk("Root node in device tree was not 'cpu' cannot continue.\n");
    halt();
  };

#ifdef DEBUG_PROBING
  printk("String address for d_type: 0x%x\n", (unsigned int) str);
  printk("str[0] = %c  str[1] = %c  str[2] = %c \n", str[0], str[1], str[2]);
#endif

  str = get_str_from_prom(nd, "name", promstr_buf);

#ifdef DEBUG_PROBING
  printk("String address for name: 0x%x\n", (unsigned int) str);
  printk("str[0] = %c  str[1] = %c  str[2] = %c \n", str[0], str[1], str[2]);
#endif

  printk("Name: %s \n", str);

  first_descent = nd = node_get_child(nd);


/* Ok, here will go a call to each specific device probe. We can
 * call these now that we have the 'root' node and the child of
 * this node to send to the routines. ORDER IS IMPORTANT!
 */

  probe_cpu();
  probe_vac();
  probe_mmu();
  phys_bytes_of_ram = probe_memory();

  printk("Physical Memory: %d bytes\n", (int) phys_bytes_of_ram);
  for(i=0; sp_banks[i].num_bytes != 0; i++) {
    printk("Bank %d:  base 0x%x  bytes %d\n", i,
	   (unsigned int) sp_banks[i].base_addr, 
	   (int) sp_banks[i].num_bytes);
    end_of_phys_memory = sp_banks[i].base_addr + sp_banks[i].num_bytes;
  }

  printk("PROM Root Child Node: 0x%x Name: %s \n", nd,
	 get_str_from_prom(nd, "name", promstr_buf));

  while((nd = node_get_sibling(nd)) != 0) {
    printk("Node: 0x%x Name: %s", nd,
	   get_str_from_prom(nd, "name", promstr_buf));
    printk("\n");
  };

  printk("\nProbing SBUS:\n");
  probe_sbus(first_descent);

  return;
}
