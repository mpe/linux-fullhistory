/* pgtsfmmu.h:  Spitfire V9 MMU support goes here.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_PGTSFMMU_H
#define _SPARC_PGTSFMMU_H

/* Spitfire is four-level.... I think... It also has a seperate TLB for
 * data and instruction mappings.
 */
#define SFMMU_PMD_SHIFT       16
#define SFMMU_PMD_SIZE        (1UL << SFMMU_PMD_SHIFT)
#define SFMMU_PMD_MASK        (~(SFMMU_PMD_SIZE-1))
#define SFMMU_PMD_ALIGN(addr) (((addr)+SFMMU_PMD_SIZE-1)&SFMMU_PMD_MASK)

#define SFMMU_PGDIR_SHIFT     19
#define SFMMU_PGDIR_SIZE        (1UL << SFMMU_PGDIR_SHIFT)
#define SFMMU_PGDIR_MASK        (~(SFMMU_PGDIR_SIZE-1))
#define SFMMU_PGDIR_ALIGN(addr) (((addr)+SFMMU_PGDIR_SIZE-1)&SFMMU_PGDIR_MASK)

#define SFMMU_PGMAP_SHIFT     22
#define SFMMU_PGDIR_SIZE        (1UL << SFMMU_PGDIR_SHIFT)
#define SFMMU_PGDIR_MASK        (~(SFMMU_PGDIR_SIZE-1))
#define SFMMU_PGDIR_ALIGN(addr) (((addr)+SFMMU_PGDIR_SIZE-1)&SFMMU_PGDIR_MASK)


#endif /* !(_SPARC_PGTSFMMU_H) */

