/* drivers/atm/atmdev_init.c - ATM device driver initialization */
 
/* Written 1995-1997 by Werner Almesberger, EPFL LRC */
 

#include <linux/config.h>
#include <linux/init.h>


#ifdef CONFIG_ATM_ENI
extern int eni_detect(void);
#endif
#ifdef CONFIG_ATM_ZATM
extern int zatm_detect(void);
#endif
#ifdef CONFIG_ATM_TNETA1570
extern int tneta1570_detect(void);
#endif
#ifdef CONFIG_ATM_FORE200
extern int fore200_detect(void);
#endif
#ifdef CONFIG_ATM_NICSTAR
extern int nicstar_detect(void);
#endif
#ifdef CONFIG_ATM_AMBASSADOR
extern int amb_detect(void);
#endif
#ifdef CONFIG_ATM_HORIZON
extern int hrz_detect(void);
#endif


int __init atmdev_init(void)
{
	int devs;

	devs = 0;
#ifdef CONFIG_ATM_ENI
	devs += eni_detect();
#endif
#ifdef CONFIG_ATM_ZATM
	devs += zatm_detect();
#endif
#ifdef CONFIG_ATM_TNETA1570
	devs += tneta1570_detect();
#endif
#ifdef CONFIG_ATM_FORE200
	devs += fore200_detect();
#endif
#ifdef CONFIG_ATM_NICSTAR
	devs += nicstar_detect();
#endif
#ifdef CONFIG_ATM_AMBASSADOR
	devs += amb_detect();
#endif
#ifdef CONFIG_ATM_HORIZON
	devs += hrz_detect();
#endif
	return devs;
}
