#include <linux/config.h>
#ifdef CONFIG_1GB
#define PAGE_OFFSET_RAW 0xC0000000
#elif defined(CONFIG_2GB)
#define PAGE_OFFSET_RAW 0x80000000
#elif defined(CONFIG_3GB)
#define PAGE_OFFSET_RAW 0x40000000
#endif
