#ifndef W83977AF_H
#define W83977AF_H

#define W977_EFER 0x370
#define W977_EFIR 0x370
#define W977_EFDR 0x371
#define W977_DEVICE_IR 0x06


/*
 * Enter extended function mode
 */
static inline void w977_efm_enter(void)
{
        outb(0x87, W977_EFER);
        outb(0x87, W977_EFER);
}

/*
 * Select a device to configure 
 */

static inline void w977_select_device(__u8 devnum)
{
	outb(0x07, W977_EFIR);
	outb(devnum, W977_EFDR);
} 

/* 
 * Write a byte to a register
 */
static inline void w977_write_reg(__u8 reg, __u8 value)
{
	outb(reg, W977_EFIR);
	outb(value, W977_EFDR);
}

/*
 * read a byte from a register
 */
static inline __u8 w977_read_reg(__u8 reg)
{
	outb(reg, W977_EFIR);
	return inb(W977_EFDR);
}

/*
 * Exit extended function mode
 */
static inline void w977_efm_exit(void)
{
	outb(0xAA, W977_EFER);
}
#endif
