#ifndef _LINUX_HEAD_H
#define _LINUX_HEAD_H

struct desc_struct {
	unsigned long a,b;
};

extern struct desc_struct idt_table[],gdt_table[];
extern struct desc_struct *idt, *gdt;

struct Xgt_desc_struct {
	unsigned short size;
	unsigned long address __attribute__((packed));
};

#define idt_descr (*(struct Xgt_desc_struct *)((char *)&idt - 2))
#define gdt_descr (*(struct Xgt_desc_struct *)((char *)&gdt - 2))

#define GDT_NUL 0
#define GDT_CODE 1
#define GDT_DATA 2
#define GDT_TMP 3

#define LDT_NUL 0
#define LDT_CODE 1
#define LDT_DATA 2

#endif
