/*
 * drivers/pcmcia/sa1100_cerf.c
 *
 * PCMCIA implementation routines for CerfBoard
 * Based off the Assabet.
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

#ifdef CONFIG_SA1100_CERF_CPLD
#define CERF_SOCKET	0
#else
#define CERF_SOCKET	1
#endif

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ IRQ_GPIO_CF_CD,   "CF_CD"   },
	{ IRQ_GPIO_CF_BVD2, "CF_BVD2" },
	{ IRQ_GPIO_CF_BVD1, "CF_BVD1" }
};

static int cerf_pcmcia_init(struct pcmcia_init *init)
{
  int i, res;

  set_irq_type(IRQ_GPIO_CF_IRQ, IRQT_FALLING);

  for (i = 0; i < ARRAY_SIZE(irqs); i++) {
    set_irq_type(irqs[i].irq, IRQT_NOEDGE);
    res = request_irq(irqs[i].irq, init->handler, SA_INTERRUPT,
		      irqs[i].str, NULL);
    if (res)
      goto irq_err;
  }

  return 2;

 irq_err:
  printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
	 __FUNCTION__, irqs[i].irq, res);

  while (i--)
    free_irq(irqs[i].irq, NULL);

  return res;
}

static int cerf_pcmcia_shutdown(void)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(irqs); i++)
    free_irq(irqs[i].irq, NULL);

  return 0;
}

static int cerf_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array){
  unsigned long levels;
  int i = CERF_SOCKET;

  if(state_array->size<2) return -1;

  levels=GPLR;

  state_array->state[i].detect=((levels & GPIO_CF_CD)==0)?1:0;
  state_array->state[i].ready=(levels & GPIO_CF_IRQ)?1:0;
  state_array->state[i].bvd1=(levels & GPIO_CF_BVD1)?1:0;
  state_array->state[i].bvd2=(levels & GPIO_CF_BVD2)?1:0;
  state_array->state[i].wrprot=0;
  state_array->state[i].vs_3v=1;
  state_array->state[i].vs_Xv=0;

  return 1;
}

static int cerf_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  if(info->sock>1) return -1;

  if (info->sock == CERF_SOCKET)
    info->irq=IRQ_GPIO_CF_IRQ;

  return 0;
}

static int cerf_pcmcia_configure_socket(const struct pcmcia_configure
					   *configure)
{
  if(configure->sock>1)
    return -1;

  if (configure->sock != CERF_SOCKET)
    return 0;

  switch(configure->vcc){
  case 0:
    break;

  case 50:
  case 33:
#ifdef CONFIG_SA1100_CERF_CPLD
     GPCR = GPIO_PWR_SHUTDOWN;
#endif
     break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    return -1;
  }

  if(configure->reset)
  {
#ifdef CONFIG_SA1100_CERF_CPLD
    GPSR = GPIO_CF_RESET;
#endif
  }
  else
  {
#ifdef CONFIG_SA1100_CERF_CPLD
    GPCR = GPIO_CF_RESET;
#endif
  }

  return 0;
}

static int cerf_pcmcia_socket_init(int sock)
{
  int i;

  if (sock == CERF_SOCKET)
    for (i = 0; i < ARRAY_SIZE(irqs); i++)
      set_irq_type(irqs[i].irq, IRQT_BOTHEDGE);

  return 0;
}

static int cerf_pcmcia_socket_suspend(int sock)
{
  int i;

  if (sock == CERF_SOCKET)
    for (i = 0; i < ARRAY_SIZE(irqs); i++)
      set_irq_type(irqs[i].irq, IRQT_NOEDGE);

  return 0;
}

static struct pcmcia_low_level cerf_pcmcia_ops = { 
  .init			= cerf_pcmcia_init,
  .shutdown		= cerf_pcmcia_shutdown,
  .socket_state		= cerf_pcmcia_socket_state,
  .get_irq_info		= cerf_pcmcia_get_irq_info,
  .configure_socket	= cerf_pcmcia_configure_socket,

  .socket_init		= cerf_pcmcia_socket_init,
  .socket_suspend	= cerf_pcmcia_socket_suspend,
};

int __init pcmcia_cerf_init(void)
{
	int ret = -ENODEV;

	if (machine_is_cerf())
		ret = sa1100_register_pcmcia(&cerf_pcmcia_ops);

	return ret;
}

void __exit pcmcia_cerf_exit(void)
{
	sa1100_unregister_pcmcia(&cerf_pcmcia_ops);
}
