/* probe.c: Preliminary device tree probing routines...

   Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
*/

#include <linux/kernel.h>
#include <asm/vac-ops.h>

/* #define DEBUG_PROBING */

char promstr_buf[64];         /* overkill */
unsigned int promint_buf[1];

extern int prom_node_root;
extern int num_segmaps, num_contexts;

extern int node_get_sibling(int node);
extern int node_get_child(int node);
extern char* get_str_from_prom(int node, char* name, char* value);
extern unsigned int* get_int_from_prom(int node, char* name, unsigned int *value);

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
  { 4, 1, "Texas Instruments, Inc. - unknown"},
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
  register int psr_impl, psr_vers, fpu_vers, i;
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

  return;
}

void
probe_mmu(void)
{
  find_mmu_num_segmaps();
  find_mmu_num_contexts();

  printk("\nMMU segmaps: %d     MMU contexts: %d\n", num_segmaps, 
	 num_contexts);

  return;
}

void
probe_clock(int fchild)
{
  /* TODO :> I just can't stomach it right now... */
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

  printk("\nProperty length for %s: 0x%x\n", "name", 
	 *get_int_from_prom(nd, "name", promint_buf));

  if(*get_int_from_prom(nd, "name", promint_buf) != 0)
  printk("Node: 0x%x Name: %s", nd, lbuf);

  lbuf = get_str_from_prom(nd, "device-type", promstr_buf);

  printk("\nProperty length for %s: 0x%x\n", "device_type", 
	 *get_int_from_prom(nd, "device_type", promint_buf));

  if(*get_int_from_prom(nd, "device-type", promint_buf) != 0)
    printk("Device-Type: %s ", lbuf);
  
  lbuf = get_str_from_prom(nd, "model", promstr_buf);

  printk("\nProperty length for %s: 0x%x\n", "model", 
	 *get_int_from_prom(nd, "model", promint_buf));

  if(*get_int_from_prom(nd, "model", promint_buf) != 0)
    printk("Model: %s", lbuf);

  printk("\n");

  while((nd = node_get_sibling(nd)) != 0)
    {
      lbuf = get_str_from_prom(nd, "name", promstr_buf);

      if(*get_int_from_prom(nd, "name", promint_buf) != 0)
      printk("Node: 0x%x Name: %s ", nd, lbuf);

      lbuf = get_str_from_prom(nd, "device-type", promstr_buf);

      if(*get_int_from_prom(nd, "device-type", promint_buf) != 0)
      printk("Device-Type: %s ", lbuf);

      lbuf = get_str_from_prom(nd, "model", promstr_buf);

      if(*get_int_from_prom(nd, "model", promint_buf) != 0)
      printk("Model: %s", lbuf);

      printk("\n");
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

  while((nd = node_get_sibling(nd)) != 0)
    {
      lbuf = get_str_from_prom(nd, "name", promstr_buf);
      if(lbuf[0]=='s' && lbuf[1]=='b' && lbuf[2]=='u' && lbuf[3]=='s')
	break;
    }
  nd = node_get_child(nd);

  printk("Node: 0x%x Name: %s\n", nd,
	 get_str_from_prom(nd, "name", promstr_buf));

  if(lbuf[0]=='e' && lbuf[1]=='s' && lbuf[2]=='p')
    probe_esp(nd);

  while((nd = node_get_sibling(nd)) != 0)
    {
      printk("Node: 0x%x Name: %s\n", nd,
	     get_str_from_prom(nd, "name", promstr_buf));

	  if(lbuf[0]=='e' && lbuf[1]=='s' && lbuf[2]=='p')
	    {
	      savend = nd;
	      probe_esp(nd);
	      nd = savend;
	    }
    }

  return;
}

void
probe_devices(void)
{
  register int nd, first_descent;
  register char* str;

  nd = prom_node_root;

  printk("PROBING DEVICES:\n");

  str = get_str_from_prom(nd, "device_type", promstr_buf);
  printk("Root Node: 0x%x ", nd);

#ifdef DEBUG_PROBING
  printk("String address for d_type: 0x%x\n", (unsigned int) str);
  printk("str[0] = %c  str[1] = %c  str[2] = %c \n", str[0], str[1], str[2]);
#endif

  printk("Device Type: %s ", str);

  str = get_str_from_prom(nd, "name", promstr_buf);

#ifdef DEBUG_PROBING
  printk("String address for name: 0x%x\n", (unsigned int) str);
  printk("str[0] = %c  str[1] = %c  str[2] = %c \n", str[0], str[1], str[2]);
#endif

  printk("Name: %s \n", str);

  first_descent = nd = node_get_child(nd);


/* Ok, here will go a call to each specific device probe. We can
   call these now that we have the 'root' node and the child of
   this node to send to the routines. ORDER IS IMPORTANT!
*/

  probe_cpu();
  probe_vac();
  probe_mmu();
  probe_clock(first_descent);

/*
  printk("PROM Root Child Node: 0x%x Name: %s \n", nd,
	 get_str_from_prom(nd, "name", promstr_buf));

  while((nd = node_get_sibling(nd)) != 0)
    {

      printk("Node: 0x%x Name: %s\n", nd,
	     get_str_from_prom(nd, "name", promstr_buf));

    }

  printk("\nProbing SBUS:\n");
  probe_sbus(first_descent);
*/

  return;
}
