/*
 * include/asm-ppc/serial.h
 */

#include <linux/config.h>

#ifdef CONFIG_APUS
#include <asm-m68k/serial.h>
#else

#define BASE_BAUD ( 1843200 / 16 )
  
#define SERIAL_PORT_DFNS
  
#ifdef CONFIG_SERIAL_MANY_PORTS
#define RS_TABLE_SIZE  64
#else
#define RS_TABLE_SIZE  4
#endif
  
#endif /* CONFIG_APUS */
