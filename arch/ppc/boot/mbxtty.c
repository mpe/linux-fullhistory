

/* Minimal serial functions needed to send messages out the serial
 * port on the MBX console.
 *
 * The MBX uxes SMC1 for the serial port.  We reset the port and use
 * only the first BD that EPPC-Bug set up as a character FIFO.
 *
 * It's a big hack, but I don't have time right now....I want a kernel
 * that boots.
 */
#include <linux/types.h>
#include <asm/mbx.h>
#include "../8xx_io/commproc.h"

#define CPM_CPCR	((volatile ushort *)0xfa2009c0)
#define SMC1_MODE	((volatile ushort *)0xfa200a82)
#define SMC1_TBDF	((volatile bd_t *)0xfa202c90)
#define SMC1_RBDF	((volatile bd_t *)0xfa202c10)

static cpm8xx_t	*cpmp = (cpm8xx_t *)&(((immap_t *)MBX_IMAP_ADDR)->im_cpm);

void
serial_init(void)
{
	volatile smc_t		*sp;
	volatile smc_uart_t	*up;
	volatile cbd_t	*tbdf, *rbdf;
	volatile cpm8xx_t	*cp;

	cp = cpmp;
	sp = (smc_t*)&(cp->cp_smc[0]);
	up = (smc_uart_t *)&cp->cp_dparam[PROFF_SMC1];

	/* Disable transmitter/receiver.
	*/
	sp->smc_smcm &= ~(SMCMR_REN | SMCMR_TEN);

	tbdf = (cbd_t *)&cp->cp_dpmem[up->smc_tbase];
	rbdf = (cbd_t *)&cp->cp_dpmem[up->smc_rbase];

	/* Issue a stop transmit, and wait for it.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC1, CPM_CR_STOP_TX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Make the first buffer the only buffer.
	*/
	tbdf->cbd_sc |= BD_SC_WRAP;
	rbdf->cbd_sc |= BD_SC_EMPTY | BD_SC_WRAP;

	/* Single character receive.
	*/
	up->smc_mrblr = 1;
	up->smc_maxidl = 0;

	/* Initialize Tx/Rx parameters.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC1, CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Enable transmitter/receiver.
	*/
	sp->smc_smcm |= SMCMR_REN | SMCMR_TEN;
}

void
serial_putchar(const char c)
{
	volatile cbd_t		*tbdf;
	volatile char		*buf;
	volatile smc_uart_t	*up;

	up = (smc_uart_t *)&cpmp->cp_dparam[PROFF_SMC1];
	tbdf = (cbd_t *)&cpmp->cp_dpmem[up->smc_tbase];

	/* Wait for last character to go.
	*/
	buf = (char *)tbdf->cbd_bufaddr;
	while (tbdf->cbd_sc & BD_SC_READY);

	*buf = c;
	tbdf->cbd_datlen = 1;
	tbdf->cbd_sc |= BD_SC_READY;
}

char
serial_getc()
{
	volatile cbd_t		*rbdf;
	volatile char		*buf;
	volatile smc_uart_t	*up;
	char			c;

	up = (smc_uart_t *)&cpmp->cp_dparam[PROFF_SMC1];
	rbdf = (cbd_t *)&cpmp->cp_dpmem[up->smc_rbase];

	/* Wait for character to show up.
	*/
	buf = (char *)rbdf->cbd_bufaddr;
	while (rbdf->cbd_sc & BD_SC_EMPTY);
	c = *buf;
	rbdf->cbd_sc |= BD_SC_EMPTY;

	return(c);
}

int
serial_tstc()
{
	volatile cbd_t		*rbdf;
	volatile smc_uart_t	*up;

	up = (smc_uart_t *)&cpmp->cp_dparam[PROFF_SMC1];
	rbdf = (cbd_t *)&cpmp->cp_dpmem[up->smc_rbase];

	return(!(rbdf->cbd_sc & BD_SC_EMPTY));
}
