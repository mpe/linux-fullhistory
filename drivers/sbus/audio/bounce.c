/*
 * drivers/sbus/audio/bounce.c
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 *
 * Simple bounce buffer allocator used for allocating pages for use in
 * DMA and pseudo-DMA (byte-by-byte using an interrupt)
 * applications. For safety, we do most operations in bottom half so
 * we have some guarnatee of atomicity. System calls can do
 * "start_bh_atomic" to get some atomicity.
 */

#include <linux/kernel.h>
#include <linux/malloc.h>
#include "bounce.h"


static struct bounce_page *pages[NR_BOUNCE_PAGES];
static struct bounce_page *free_list_head, *free_list_tail;

/* Add a bounce page to the free list. */
static void add_bounce_page(struct bounce_page *page)
{
  unsigned long flags;

#ifdef DEBUG_BOUNCE
  if (page->next) {
    printk(KERN_DEBUG "add_bounce_page: page already on list from %p\n",
	   __builtin_return_address(0));
    return;
  }
#endif

  save_flags(flags);
  cli();

  /* Case #1: Nothing on the list. */
  if (!free_list_tail) {
#ifdef DEBUG_BOUNCE
    if (free_list_head) {
      printk(KERN_DEBUG "add_bounce_page: inconsistent free list from %p\n",
	     __builtin_return_address(0));
      restore_flags(flags);
      return;
    }
#endif
    free_list_head = free_list_tail = page;
  } else {
    free_list_tail->next = page;
    page->next = NULL;
    free_list_tail = page;
  }

  restore_flags(flags);
}

/* Remove a bounce page from the free list. */
static struct bounce_page *next_bounce_page(void)
{
  struct bounce_page *p;
  unsigned long flags;

  save_flags(flags);
  cli();

  /* If the free list is empty, return empty handed. */
  if (!free_list_head) {
    restore_flags(flags);
    return NULL;
  }

  /* Remove the next available bounce page from the free list. */
  p = free_list_head;

  /* Update the free list pointers. */
  free_list_head = free_list_head->next;
  if (!free_list_head)
    free_list_tail = NULL;

  /* Return the page that we found. */
  p->next = NULL;
  restore_flags(flags);
  return p;
}

/* Allocate the bounce buffers and the page lists. */
int bounce_init(void)
{
  register int i;

  /* Allocate space for all of the bounce pages. */
  for (i = 0; i < NR_BOUNCE_PAGES; i++) {
    pages[i] = __get_free_page(GFP_KERNEL);
    if (!pages[i]) {
      register int j;
      for (j = 0; j < i; j++) free_page(pages[i]);
      return -ENOMEM;
    }
  }

  /* Place all of the pages onto the free list. */
  for (i = 0; i < NR_BOUNCE_PAGES - 1; i++)
    pages[i]->next = pages[i+1];
  pages[NR_BOUNCE_BUFFERS-1]->next = NULL;

  /* Setup pointers to the free list head and tail. */
  free_list_head = pages[0];
  free_list_tail = pages[NR_BOUNCE_BUFFERS-1];
  return 0;
}

/* Allocate a bounce buffer to a process. */
struct bounce_page * get_bounce_page(int timeout)
{
  struct bounce_page *p;
  int tries;

  do {
    /* Do not allow interrupts to muck with the lists. */
    start_bh_atomic();

    /* Check to see if a bounce page is available. */
    p = next_bounce_page();
    if (p) {
      end_bh_atomic();
      return p;
    }

    /* No bounce page was available. Sleep and wait for one. */
    end_bh_atomic();
    current->state = TASK_INTERRUPTIBLE;
    current->timeout = jiffies + HZ / 10;
    schedule();

    /* If we received a signal, then bail out. */
    if (current->signal & ~current->blocked)
      return NULL;
    
  } while (jiffies <= timeout);

  /* We did not find anything before the timeout. */
  return NULL;
}

/* Return a bounce buffer to the free list. */
void put_bounce_page(struct bounce_page *page)
{
  add_bounce_page(page);
}

