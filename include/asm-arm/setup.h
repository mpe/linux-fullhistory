/*
 * include/asm/setup.h
 *
 * Structure passed to kernel to tell it about the hardware it's running on
 *
 * Copyright (C) 1997,1998 Russell King
 */
#ifndef __ASMARM_SETUP_H
#define __ASMARM_SETUP_H

struct param_struct {
    union {
	struct {
	    unsigned long page_size;		/*  0 */
	    unsigned long nr_pages;		/*  4 */
	    unsigned long ramdisk_size;		/*  8 */
	    unsigned long flags;		/* 12 */
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
	    unsigned long rootdev;		/* 16 */
	    unsigned long video_num_cols;	/* 20 */
	    unsigned long video_num_rows;	/* 24 */
	    unsigned long video_x;		/* 28 */
	    unsigned long video_y;		/* 32 */
	    unsigned long memc_control_reg;	/* 36 */
	    unsigned char sounddefault;		/* 40 */
	    unsigned char adfsdrives;		/* 41 */
	    unsigned char bytes_per_char_h;	/* 42 */
	    unsigned char bytes_per_char_v;	/* 43 */
	    unsigned long pages_in_bank[4];	/* 44 */
	    unsigned long pages_in_vram;	/* 60 */
	    unsigned long initrd_start;		/* 64 */
	    unsigned long initrd_size;		/* 68 */
	    unsigned long rd_start;		/* 72 */
	} s;
	char unused[256];
    } u1;
    union {
	char paths[8][128];
	struct {
	    unsigned long magic;
	    char n[1024 - sizeof(unsigned long)];
	} s;
    } u2;
    char commandline[256];
};

#endif
