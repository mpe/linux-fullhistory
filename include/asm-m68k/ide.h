#ifndef _M68K_IDE_H
#define _M68K_IDE_H

/* Copyright(c) 1996 Kars de Jong */
/* Based on the ide driver from 1.2.13pl8 */

#include <linux/config.h>

#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amihdreg.h>
#include <asm/amigaints.h>
#endif /* CONFIG_AMIGA */

#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atarihdreg.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#endif /* CONFIG_ATARI */

#include <asm/bootinfo.h>

struct hd_regs_struct {
  unsigned int hd_error,
  hd_nsector,
  hd_sector,
  hd_lcyl,
  hd_hcyl,
  hd_select,
  hd_status;
};

static struct hd_regs_struct hd_regs;
static void probe_m68k_ide (void);

/* Undefine these again, they were defined for the PC. */
#undef IDE_ERROR_OFFSET
#undef IDE_NSECTOR_OFFSET
#undef IDE_SECTOR_OFFSET
#undef IDE_LCYL_OFFSET
#undef IDE_HCYL_OFFSET
#undef IDE_SELECT_OFFSET
#undef IDE_STATUS_OFFSET
#undef IDE_FEATURE_OFFSET
#undef IDE_COMMAND_OFFSET
#undef SELECT_DRIVE

#define IDE_ERROR_OFFSET	hd_regs.hd_error
#define IDE_NSECTOR_OFFSET	hd_regs.hd_nsector
#define IDE_SECTOR_OFFSET	hd_regs.hd_sector
#define IDE_LCYL_OFFSET		hd_regs.hd_lcyl
#define IDE_HCYL_OFFSET		hd_regs.hd_hcyl
#define IDE_SELECT_OFFSET	hd_regs.hd_select
#define IDE_STATUS_OFFSET	hd_regs.hd_status
#define IDE_FEATURE_OFFSET	IDE_ERROR_OFFSET
#define IDE_COMMAND_OFFSET	IDE_STATUS_OFFSET

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

#undef HD_DATA
#define HD_DATA NULL

#define SELECT_DRIVE(hwif,drive)  OUT_BYTE((drive)->select.all, hwif->io_base+IDE_SELECT_OFFSET);

#define insl(data_reg, buffer, wcount) insw(data_reg, buffer, wcount<<1)
#define outsl(data_reg, buffer, wcount) outsw(data_reg, buffer, wcount<<1)

#define insw(port, buf, nr) \
    if (nr % 16) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/a1@+; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #4,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6");

#define outsw(port, buf, nr) \
    if (nr % 16) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/a0@; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #4,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6");

#define T_CHAR          (0x0000)        /* char:  don't touch  */
#define T_SHORT         (0x4000)        /* short: 12 -> 21     */
#define T_INT           (0x8000)        /* int:   1234 -> 4321 */
#define T_TEXT          (0xc000)        /* text:  12 -> 21     */

#define T_MASK_TYPE     (0xc000)
#define T_MASK_COUNT    (0x3fff)

#define D_CHAR(cnt)     (T_CHAR  | (cnt))
#define D_SHORT(cnt)    (T_SHORT | (cnt))
#define D_INT(cnt)      (T_INT   | (cnt))
#define D_TEXT(cnt)     (T_TEXT  | (cnt))

static u_short driveid_types[] = {
	D_SHORT(10),	/* config - vendor2 */
	D_TEXT(20),	/* serial_no */
	D_SHORT(3),	/* buf_type - ecc_bytes */
	D_TEXT(48),	/* fw_rev - model */
	D_CHAR(2),	/* max_multsect - vendor3 */
	D_SHORT(1),	/* dword_io */
	D_CHAR(2),	/* vendor4 - capability */
	D_SHORT(1),	/* reserved50 */
	D_CHAR(4),	/* vendor5 - tDMA */
	D_SHORT(4),	/* field_valid - cur_sectors */
	D_INT(1),	/* cur_capacity */
	D_CHAR(2),	/* multsect - multsect_valid */
	D_INT(1),	/* lba_capacity */
	D_SHORT(194)	/* dma_1word - reservedyy */
};

#define num_driveid_types       (sizeof(driveid_types)/sizeof(*driveid_types))

static __inline__ void big_endianize_driveid(struct hd_driveid *id)
{
   u_char *p = (u_char *)id;
   int i, j, cnt;
   u_char t;

   for (i = 0; i < num_driveid_types; i++) {
      cnt = driveid_types[i] & T_MASK_COUNT;
      switch (driveid_types[i] & T_MASK_TYPE) {
         case T_CHAR:
            p += cnt;
            break;
         case T_SHORT:
            for (j = 0; j < cnt; j++) {
               t = p[0];
               p[0] = p[1];
               p[1] = t;
               p += 2;
            }
            break;
         case T_INT:
            for (j = 0; j < cnt; j++) {
               t = p[0];
               p[0] = p[3];
               p[3] = t;
               t = p[1];
               p[1] = p[2];
               p[2] = t;
               p += 4;
            }
            break;
         case T_TEXT:
            for (j = 0; j < cnt; j += 2) {
               t = p[0];
               p[0] = p[1];
               p[1] = t;
               p += 2;
            }
            break;
      }
   }
}

#endif /* _M68K_IDE_H */
