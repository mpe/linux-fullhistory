
#include <linux/config.h>

#ifdef CONFIG_ISDN_DRV_ICN
extern void icn_init(void);
#endif

#ifdef CONFIG_ISDN_DRV_TELES
extern void teles_init(void);
#endif

void isdn_cards_init(void)
{
#if CONFIG_ISDN_DRV_ICN
        icn_init();
#endif
#if CONFIG_ISDN_DRV_TELES
        teles_init();
#endif
}

