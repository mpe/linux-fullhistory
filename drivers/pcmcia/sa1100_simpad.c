/*
 * drivers/pcmcia/sa1100_simpad.c
 *
 * PCMCIA implementation routines for simpad
 *
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include "sa1100_generic.h"
 
extern long get_cs3_shadow(void);
extern void set_cs3_bit(int value); 
extern void clear_cs3_bit(int value);


static int simpad_pcmcia_init(struct pcmcia_init *init){
  int irq, res;

  set_cs3_bit(PCMCIA_RESET);
  clear_cs3_bit(PCMCIA_BUFF_DIS);
  clear_cs3_bit(PCMCIA_RESET);

  clear_cs3_bit(VCC_3V_EN|VCC_5V_EN|EN0|EN1);

  /* Set transition detect */
  set_irq_type( IRQ_GPIO_CF_CD, IRQT_NOEDGE );
  set_irq_type( IRQ_GPIO_CF_IRQ, IRQT_FALLING );

  /* Register interrupts */
  irq = IRQ_GPIO_CF_CD;
  res = request_irq( irq, init->handler, SA_INTERRUPT, "CF_CD", NULL );
  if( res < 0 ) goto irq_err;

  /* There's only one slot, but it's "Slot 1": */
  return 2;

irq_err:
  printk( KERN_ERR "%s: request for IRQ%d failed (%d)\n",
	 __FUNCTION__, irq, res);
  return res;
}

static int simpad_pcmcia_shutdown(void)
{
  /* disable IRQs */
  free_irq( IRQ_GPIO_CF_CD, NULL );
  
  /* Disable CF bus: */
  
  //set_cs3_bit(PCMCIA_BUFF_DIS);
  clear_cs3_bit(PCMCIA_RESET);       
  
  return 0;
}

static int simpad_pcmcia_socket_state(struct pcmcia_state_array
				       *state_array)
{
  unsigned long levels;
  unsigned long *cs3reg = CS3_BASE;

  if(state_array->size<2) return -1;

  memset(state_array->state, 0, 
	 (state_array->size)*sizeof(struct pcmcia_state));

  levels=GPLR;

  state_array->state[1].detect=((levels & GPIO_CF_CD)==0)?1:0;

  state_array->state[1].ready=(levels & GPIO_CF_IRQ)?1:0;

  state_array->state[1].bvd1=1; /* Not available on Simpad. */

  state_array->state[1].bvd2=1; /* Not available on Simpad. */

  state_array->state[1].wrprot=0; /* Not available on Simpad. */

  
  if((*cs3reg & 0x0c) == 0x0c) {
    state_array->state[1].vs_3v=0;
    state_array->state[1].vs_Xv=0;
  } else
  {
    state_array->state[1].vs_3v=1;
    state_array->state[1].vs_Xv=0;
  }
  return 1;
}

static int simpad_pcmcia_get_irq_info(struct pcmcia_irq_info *info){

  if(info->sock>1) return -1;

  if(info->sock==1)
    info->irq=IRQ_GPIO_CF_IRQ;

  return 0;
}

static int simpad_pcmcia_configure_socket(int sock, const struct pcmcia_configure
					   *configure)
{
  unsigned long value, flags;

  if(sock>1) return -1;

  if(sock==0) return 0;

  local_irq_save(flags);

  /* Murphy: see table of MIC2562a-1 */

  switch(configure->vcc){
  case 0:
    clear_cs3_bit(VCC_3V_EN|VCC_5V_EN|EN0|EN1);
    break;

  case 33:  
    clear_cs3_bit(VCC_3V_EN|EN0);
    set_cs3_bit(VCC_5V_EN|EN1);
    break;

  case 50:
    clear_cs3_bit(VCC_5V_EN|EN1);
    set_cs3_bit(VCC_3V_EN|EN0);
    break;

  default:
    printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
	   configure->vcc);
    clear_cs3_bit(VCC_3V_EN|VCC_5V_EN|EN0|EN1);
    local_irq_restore(flags);
    return -1;
  }

  /* Silently ignore Vpp, output enable, speaker enable. */

  local_irq_restore(flags);

  return 0;
}

static int simpad_pcmcia_socket_init(int sock)
{
  set_irq_type(IRQ_GPIO_CF_CD, IRQT_BOTHEDGE);
  return 0;
}

static int simpad_pcmcia_socket_suspend(int sock)
{
  set_irq_type(IRQ_GPIO_CF_CD, IRQT_NOEDGE);
  return 0;
}

static struct pcmcia_low_level simpad_pcmcia_ops = { 
  .init			= simpad_pcmcia_init,
  .shutdown		= simpad_pcmcia_shutdown,
  .socket_state		= simpad_pcmcia_socket_state,
  .get_irq_info		= simpad_pcmcia_get_irq_info,
  .configure_socket	= simpad_pcmcia_configure_socket,

  .socket_init		= simpad_pcmcia_socket_init,
  .socket_suspend	= simpad_pcmcia_socket_suspend,
};

int __init pcmcia_simpad_init(void)
{
	int ret = -ENODEV;

	if (machine_is_simpad())
		ret = sa1100_register_pcmcia(&simpad_pcmcia_ops);

	return ret;
}

void __exit pcmcia_simpad_exit(void)
{
	sa1100_unregister_pcmcia(&simpad_pcmcia_ops);
}
