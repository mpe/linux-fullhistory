/*
 *   serial.c
 *   Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
 *                    Isaku Yamahata <yamahata@private.email.ne.jp>,
 *		      George Hansper <ghansper@apana.org.au>,
 *		      Hannu Savolainen
 *
 *   This code is based on the code from ALSA 0.5.9, but heavily rewritten.
 *
 * Sat Mar 31 17:27:57 PST 2001 tim.mann@compaq.com 
 *      Added support for the Midiator MS-124T and for the MS-124W in
 *      Single Addressed (S/A) or Multiple Burst (M/B) mode, with
 *      power derived either parasitically from the serial port or
 *      from a separate power supply.
 * 
 *      The new snd_adaptor module parameter allows you to select
 *      either the default Roland Soundcanvas support (0), which was
 *      previously included in this driver but was not documented,
 *      Midiator MS-124T support (1), Midiator MS-124W S/A mode
 *      support (2), or MS-124W M/B mode support (3).  For the
 *      Midiator MS-124W, you must set the physical M-S and A-B
 *      switches on the Midiator to match the driver mode you select.
 *  
 *      - In Roland Soundcanvas mode, multiple ALSA raw MIDI
 *      substreams are supported (midiCnD0-midiCnD15).  Whenever you
 *      write to a different substream, the driver sends the
 *      nonstandard MIDI command sequence F5 NN, where NN is the
 *      substream number plus 1.  Roland modules use this command to
 *      switch between different "parts", so this feature lets you
 *      treat each part as a distinct raw MIDI substream.  The driver
 *      provides no way to send F5 00 (no selection) or to not send
 *      the F5 NN command sequence at all; perhaps it ought to.
 * 
 *      - In MS-124T mode, one raw MIDI substream is supported
 *      (midiCnD0); the snd_outs module parameter is automatically set
 *      to 1.  The driver sends the same data to all four MIDI Out
 *      connectors.  Set the A-B switch and the snd_speed module
 *      parameter to match (A=19200, B=9600).
 * 
 *      Usage example for MS-124T, with A-B switch in A position:
 *        setserial /dev/ttyS0 uart none
 *        /sbin/modprobe snd-card-serial snd_port=0x3f8 snd_irq=4 \
 *            snd_adaptor=1 snd_speed=19200
 *
 *      - In MS-124W S/A mode, one raw MIDI substream is supported
 *      (midiCnD0); the snd_outs module parameter is automatically set
 *      to 1.  The driver sends the same data to all four MIDI Out
 *      connectors at full MIDI speed.
 * 
 *      Usage example for S/A mode:
 *        setserial /dev/ttyS0 uart none
 *        /sbin/modprobe snd-card-serial snd_port=0x3f8 snd_irq=4 \
 *            snd_adaptor=2
 *
 *      - In MS-124W M/B mode, the driver supports 16 ALSA raw MIDI
 *      substreams; the snd_outs module parameter is automatically set
 *      to 16.  The substream number gives a bitmask of which MIDI Out
 *      connectors the data should be sent to, with midiCnD1 sending
 *      to Out 1, midiCnD2 to Out 2, midiCnD4 to Out 3, and midiCnD8
 *      to Out 4.  Thus midiCnD15 sends the data to all 4 ports.  As a
 *      special case, midiCnD0 also sends to all ports, since it is
 *      not useful to send the data to no ports.  M/B mode has extra
 *      overhead to select the MIDI Out for each byte, so the
 *      aggregate data rate across all four MIDI Outs is at most one
 *      byte every 520 us, as compared with the full MIDI data rate of
 *      one byte every 320 us per port.
 * 
 *      Usage example for M/B mode:
 *        setserial /dev/ttyS0 uart none
 *        /sbin/insmod snd-card-serial snd_port=0x3f8 snd_irq=4 \
 *            snd_adaptor=3
 *
 *      - The MS-124W hardware's M/A mode is currently not supported.
 *      This mode allows the MIDI Outs to act independently at double
 *      the aggregate throughput of M/B, but does not allow sending
 *      the same byte simultaneously to multiple MIDI Outs.  The M/A
 *      protocol requires the driver to twiddle the modem control
 *      lines under timing constraints, so it would be a bit more
 *      complicated to implement than the other modes.
 *
 *      - Midiator models other than MS-124W and MS-124T are currently
 *      not supported.  Note that the suffix letter is significant;
 *      the MS-124 and MS-124B are not compatible, nor are the other
 *      known models MS-101, MS-101B, MS-103, and MS-114.  I do have
 *      documentation that partially covers these models, but no units
 *      to experiment with.  The MS-124W support is tested with a real
 *      unit.  The MS-124T support is untested, but should work.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#include <linux/serial_reg.h>

EXPORT_NO_SYMBOLS;
MODULE_DESCRIPTION("MIDI serial");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{ALSA, MIDI serial}}");

#define SNDRV_SERIAL_SOUNDCANVAS 0 /* Roland Soundcanvas; F5 NN selects part */
#define SNDRV_SERIAL_MS124T 1      /* Midiator MS-124T */
#define SNDRV_SERIAL_MS124W_SA 2   /* Midiator MS-124W in S/A mode */
#define SNDRV_SERIAL_MS124W_MB 3   /* Midiator MS-124W in M/B mode */
#define SNDRV_SERIAL_MAX_ADAPTOR SNDRV_SERIAL_MS124W_MB
static char *adaptor_names[] = {
	"Soundcanvas",
        "MS-124T",
	"MS-124W S/A",
	"MS-124W M/B"
};

static int snd_index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *snd_id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int snd_enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long snd_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;     /* 0x3f8,0x2f8,0x3e8,0x2e8 */
static int snd_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 3,4,5,7,9,10,11,14,15 */
static int snd_speed[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 38400}; /* 9600,19200,38400,57600,115200 */
static int snd_base[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 115200}; /* baud base */
static int snd_outs[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 1};	/* 1 to 16 */
static int snd_adaptor[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = SNDRV_SERIAL_SOUNDCANVAS};

MODULE_PARM(snd_index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_index, "Index value for Serial MIDI.");
MODULE_PARM_SYNTAX(snd_index, SNDRV_INDEX_DESC);
MODULE_PARM(snd_id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(snd_id, "ID string for Serial MIDI.");
MODULE_PARM_SYNTAX(snd_id, SNDRV_ID_DESC);
MODULE_PARM(snd_enable, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_enable, "Enable UART16550A chip.");
MODULE_PARM_SYNTAX(snd_enable, SNDRV_ENABLE_DESC);
MODULE_PARM(snd_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(snd_port, "Port # for UART16550A chip.");
MODULE_PARM_SYNTAX(snd_port, SNDRV_PORT12_DESC);
MODULE_PARM(snd_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_irq, "IRQ # for UART16550A chip.");
MODULE_PARM_SYNTAX(snd_irq, SNDRV_IRQ_DESC);
MODULE_PARM(snd_speed, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_speed, "Speed in bauds.");
MODULE_PARM_SYNTAX(snd_speed, SNDRV_ENABLED ",allows:{9600,19200,38400,57600,115200},dialog:list");
MODULE_PARM(snd_base, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_base, "Base for divisor in bauds.");
MODULE_PARM_SYNTAX(snd_base, SNDRV_ENABLED ",allows:{57600,115200,230400,460800},dialog:list");
MODULE_PARM(snd_outs, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_outs, "Number of MIDI outputs.");
MODULE_PARM_SYNTAX(snd_outs, SNDRV_ENABLED ",allows:{{1,16}},dialog:list");
MODULE_PARM(snd_adaptor, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(snd_adaptor, "Type of adaptor.");
MODULE_PARM_SYNTAX(snd_adaptor, SNDRV_ENABLED ",allows:{{0=Soundcanvas,1=MS-124T,2=MS-124W S/A,3=MS-124W M/B}},dialog:list");

/*#define SNDRV_SERIAL_MS124W_MB_NOCOMBO 1*/  /* Address outs as 0-3 instead of bitmap */

#define SNDRV_SERIAL_MAX_OUTS	16		/* max 64, min 16 */

#define TX_BUFF_SIZE		(1<<9)		/* Must be 2^n */
#define TX_BUFF_MASK		(TX_BUFF_SIZE - 1)

#define SERIAL_MODE_NOT_OPENED 		(0)
#define SERIAL_MODE_INPUT_OPEN		(1 << 0)
#define SERIAL_MODE_OUTPUT_OPEN		(1 << 1)
#define SERIAL_MODE_INPUT_TRIGGERED	(1 << 2)
#define SERIAL_MODE_OUTPUT_TRIGGERED	(1 << 3)

typedef struct _snd_uart16550 {
	snd_card_t *card;
	snd_rawmidi_t *rmidi;
	snd_rawmidi_substream_t *midi_output[SNDRV_SERIAL_MAX_OUTS];
	snd_rawmidi_substream_t *midi_input;

	int filemode;		//open status of file

	spinlock_t open_lock;

	int irq;

	unsigned long base;
	struct resource *res_base;

	unsigned int speed;
	unsigned int speed_base;
	unsigned char divisor;

	unsigned char old_divisor_lsb;
	unsigned char old_divisor_msb;
	unsigned char old_line_ctrl_reg;

	// parameter for using of write loop
	short int fifo_limit;	//used in uart16550
        short int fifo_count;	//used in uart16550

	// type of adaptor
	int adaptor;

	// outputs
	int prev_out;
	unsigned char prev_status[SNDRV_SERIAL_MAX_OUTS];

	// write buffer and its writing/reading position
	unsigned char tx_buff[TX_BUFF_SIZE];
	int buff_in_count;
        int buff_in;
        int buff_out;

	// wait timer
	unsigned int timer_running:1;
	struct timer_list buffer_timer;

} snd_uart16550_t;

static snd_card_t *snd_serial_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;

inline static void snd_uart16550_add_timer(snd_uart16550_t *uart)
{
	if (! uart->timer_running) {
		/* timer 38600bps * 10bit * 16byte */
		uart->buffer_timer.expires = jiffies + (HZ+255)/256;
		uart->timer_running = 1;
		add_timer(&uart->buffer_timer);
	}
}

inline static void snd_uart16550_del_timer(snd_uart16550_t *uart)
{
	if (uart->timer_running) {
		del_timer(&uart->buffer_timer);
		uart->timer_running = 0;
	}
}

/* This macro is only used in snd_uart16550_io_loop */
inline static void snd_uart16550_buffer_output(snd_uart16550_t *uart)
{
	unsigned short buff_out = uart->buff_out;
	outb(uart->tx_buff[buff_out], uart->base + UART_TX);
	uart->fifo_count++;
	buff_out++;
	buff_out &= TX_BUFF_MASK;
	uart->buff_out = buff_out;
	uart->buff_in_count--;
}

/* This loop should be called with interrupts disabled
 * We don't want to interrupt this, 
 * as we're already handling an interupt 
 */
static void snd_uart16550_io_loop(snd_uart16550_t * uart)
{
	unsigned char c, status;

	/* Read Loop */
	while ((status = inb(uart->base + UART_LSR)) & UART_LSR_DR) {
		/* while receive data ready */
		c = inb(uart->base + UART_RX);
		if (uart->filemode & SERIAL_MODE_INPUT_OPEN) {
			snd_rawmidi_receive(uart->midi_input, &c, 1);
		}
		if (status & UART_LSR_OE)
			snd_printk("%s: Overrun on device at 0x%lx\n",
			       uart->rmidi->name, uart->base);
	}

	/* no need of check SERIAL_MODE_OUTPUT_OPEN because if not,
	   buffer is never filled. */
	/* Check write status */
	if (status & UART_LSR_THRE) {
		uart->fifo_count = 0;
	}
	if (uart->adaptor == SNDRV_SERIAL_MS124W_SA) {
		/* Can't use FIFO, must send only when CTS is true */
		status = inb(uart->base + UART_MSR);
		if (uart->fifo_count == 0 && (status & UART_MSR_CTS)
		    && uart->buff_in_count > 0)
			snd_uart16550_buffer_output(uart);
	} else {
		/* Write loop */
		while (uart->fifo_count < uart->fifo_limit	/* Can we write ? */
		       && uart->buff_in_count > 0)	/* Do we want to? */
			snd_uart16550_buffer_output(uart);
	}
	if (uart->irq < 0 && uart->buff_in_count > 0)
		snd_uart16550_add_timer(uart);
}

/* NOTES ON SERVICING INTERUPTS
 * ---------------------------
 * After receiving a interrupt, it is important to indicate to the UART that
 * this has been done. 
 * For a Rx interupt, this is done by reading the received byte.
 * For a Tx interupt this is done by either:
 * a) Writing a byte
 * b) Reading the IIR
 * It is particularly important to read the IIR if a Tx interupt is received
 * when there is no data in tx_buff[], as in this case there no other
 * indication that the interupt has been serviced, and it remains outstanding
 * indefinitely. This has the curious side effect that and no further interupts
 * will be generated from this device AT ALL!!.
 * It is also desirable to clear outstanding interupts when the device is
 * opened/closed.
 *
 *
 * Note that some devices need OUT2 to be set before they will generate
 * interrupts at all. (Possibly tied to an internal pull-up on CTS?)
 */
static void snd_uart16550_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	snd_uart16550_t *uart;

	uart = (snd_uart16550_t *) dev_id;
	spin_lock(&uart->open_lock);
	if (uart->filemode == SERIAL_MODE_NOT_OPENED) {
		spin_unlock(&uart->open_lock);
		return;
	}
	inb(uart->base + UART_IIR);		/* indicate to the UART that the interupt has been serviced */
	snd_uart16550_io_loop(uart);
	spin_unlock(&uart->open_lock);
}

/* When the polling mode, this function calls snd_uart16550_io_loop. */
static void snd_uart16550_buffer_timer(unsigned long data)
{
	snd_uart16550_t *uart;

	uart = (snd_uart16550_t *)data;
	spin_lock(&uart->open_lock);
	snd_uart16550_del_timer(uart);
	snd_uart16550_io_loop(uart);
	spin_unlock(&uart->open_lock);
}

/*
 *  this method probes, if an uart sits on given port
 *  return 0 if found
 *  return negative error if not found
 */
static int __init snd_uart16550_detect(unsigned int io_base)
{
	int ok;
	unsigned char c;

	if (check_region(io_base, 8))
		return -EBUSY;

	/* Do some vague tests for the presence of the uart */
	if (io_base == 0)
		return -ENODEV;	/* Not configured */

	ok = 1;			/* uart detected unless one of the following tests should fail */
	/* 8 data-bits, 1 stop-bit, parity off, DLAB = 0 */
	outb(UART_LCR_WLEN8, io_base + UART_LCR); /* Line Control Register */
	c = inb(io_base + UART_IER);
	/* The top four bits of the IER should always == 0 */
	if ((c & 0xf0) != 0)
		ok = 0;		/* failed */

	outb(0xaa, io_base + UART_SCR);
	/* Write arbitrary data into the scratch reg */
	c = inb(io_base + UART_SCR);
	/* If it comes back, it's OK */
	if (c != 0xaa)
		ok = 0;		/* failed */

	outb(0x55, io_base + UART_SCR);
	/* Write arbitrary data into the scratch reg */
	c = inb(io_base + UART_SCR);
	/* If it comes back, it's OK */
	if (c != 0x55)
		ok = 0;		/* failed */

	return ok;
}

static void snd_uart16550_do_open(snd_uart16550_t * uart)
{
	char byte;

	/* Initialize basic variables */
	uart->buff_in_count = 0;
	uart->buff_in = 0;
	uart->buff_out = 0;
	uart->fifo_limit = 1;
	uart->fifo_count = 0;
	uart->timer_running = 0;

	outb(UART_FCR_ENABLE_FIFO	/* Enable FIFO's (if available) */
	     | UART_FCR_CLEAR_RCVR	/* Clear receiver FIFO */
	     | UART_FCR_CLEAR_XMIT	/* Clear transmitter FIFO */
	     | UART_FCR_TRIGGER_4	/* Set FIFO trigger at 4-bytes */
	/* NOTE: interupt generated after T=(time)4-bytes
	 * if less than UART_FCR_TRIGGER bytes received
	 */
	     ,uart->base + UART_FCR);	/* FIFO Control Register */

	if ((inb(uart->base + UART_IIR) & 0xf0) == 0xc0)
		uart->fifo_limit = 16;
	if (uart->divisor != 0) {
		uart->old_line_ctrl_reg = inb(uart->base + UART_LCR);
		outb(UART_LCR_DLAB	/* Divisor latch access bit */
		     ,uart->base + UART_LCR);	/* Line Control Register */
		uart->old_divisor_lsb = inb(uart->base + UART_DLL);
		uart->old_divisor_msb = inb(uart->base + UART_DLM);

		outb(uart->divisor
		     ,uart->base + UART_DLL);	/* Divisor Latch Low */
		outb(0
		     ,uart->base + UART_DLM);	/* Divisor Latch High */
		/* DLAB is reset to 0 in next outb() */
	}
	/* Set serial parameters (parity off, etc) */
	outb(UART_LCR_WLEN8	/* 8 data-bits */
	     | 0		/* 1 stop-bit */
	     | 0		/* parity off */
	     | 0		/* DLAB = 0 */
	     ,uart->base + UART_LCR);	/* Line Control Register */

	switch (uart->adaptor) {
	default:
		outb(UART_MCR_RTS	/* Set Request-To-Send line active */
		     | UART_MCR_DTR	/* Set Data-Terminal-Ready line active */
		     | UART_MCR_OUT2	/* Set OUT2 - not always required, but when
					 * it is, it is ESSENTIAL for enabling interrupts
				 */
		     ,uart->base + UART_MCR);	/* Modem Control Register */
		break;
	case SNDRV_SERIAL_MS124W_SA:
	case SNDRV_SERIAL_MS124W_MB:
		/* MS-124W can draw power from RTS and DTR if they
		   are in opposite states. */ 
		outb(UART_MCR_RTS | (0&UART_MCR_DTR) | UART_MCR_OUT2,
		     uart->base + UART_MCR);
		break;
	case SNDRV_SERIAL_MS124T:
		/* MS-124T can draw power from RTS and/or DTR (preferably
		   both) if they are both asserted. */
		outb(UART_MCR_RTS | UART_MCR_DTR | UART_MCR_OUT2,
		     uart->base + UART_MCR);
		break;
	}

	if (uart->irq < 0) {
		byte = (0 & UART_IER_RDI)	/* Disable Receiver data interupt */
		    |(0 & UART_IER_THRI)	/* Disable Transmitter holding register empty interupt */
		    ;
	} else if (uart->adaptor == SNDRV_SERIAL_MS124W_SA) {
		byte = UART_IER_RDI	/* Enable Receiver data interrupt */
		    | UART_IER_MSI	/* Enable Modem status interrupt */
		    ;
	} else {
		byte = UART_IER_RDI	/* Enable Receiver data interupt */
		    | UART_IER_THRI	/* Enable Transmitter holding register empty interupt */
		    ;
	}
	outb(byte, uart->base + UART_IER);	/* Interupt enable Register */

	inb(uart->base + UART_LSR);	/* Clear any pre-existing overrun indication */
	inb(uart->base + UART_IIR);	/* Clear any pre-existing transmit interrupt */
	inb(uart->base + UART_RX);	/* Clear any pre-existing receive interrupt */
}

static void snd_uart16550_do_close(snd_uart16550_t * uart)
{
	if (uart->irq < 0)
		snd_uart16550_del_timer(uart);

	/* NOTE: may need to disable interrupts before de-registering out handler.
	 * For now, the consequences are harmless.
	 */

	outb((0 & UART_IER_RDI)		/* Disable Receiver data interupt */
	     |(0 & UART_IER_THRI)	/* Disable Transmitter holding register empty interupt */
	     ,uart->base + UART_IER);	/* Interupt enable Register */

	switch (uart->adaptor) {
	default:
		outb((0 & UART_MCR_RTS)		/* Deactivate Request-To-Send line  */
		     |(0 & UART_MCR_DTR)	/* Deactivate Data-Terminal-Ready line */
		     |(0 & UART_MCR_OUT2)	/* Deactivate OUT2 */
		     ,uart->base + UART_MCR);	/* Modem Control Register */
	  break;
	case SNDRV_SERIAL_MS124W_SA:
	case SNDRV_SERIAL_MS124W_MB:
		/* MS-124W can draw power from RTS and DTR if they
		   are in opposite states; leave it powered. */ 
		outb(UART_MCR_RTS | (0&UART_MCR_DTR) | (0&UART_MCR_OUT2),
		     uart->base + UART_MCR);
		break;
	case SNDRV_SERIAL_MS124T:
		/* MS-124T can draw power from RTS and/or DTR (preferably
		   both) if they are both asserted; leave it powered. */
		outb(UART_MCR_RTS | UART_MCR_DTR | (0&UART_MCR_OUT2),
		     uart->base + UART_MCR);
		break;
	}

	inb(uart->base + UART_IIR);	/* Clear any outstanding interupts */

	/* Restore old divisor */
	if (uart->divisor != 0) {
		outb(UART_LCR_DLAB		/* Divisor latch access bit */
		     ,uart->base + UART_LCR);	/* Line Control Register */
		outb(uart->old_divisor_lsb
		     ,uart->base + UART_DLL);	/* Divisor Latch Low */
		outb(uart->old_divisor_msb
		     ,uart->base + UART_DLM);	/* Divisor Latch High */
		/* Restore old LCR (data bits, stop bits, parity, DLAB) */
		outb(uart->old_line_ctrl_reg
		     ,uart->base + UART_LCR);	/* Line Control Register */
	}
}

static int snd_uart16550_input_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&uart->open_lock, flags);
	if (uart->filemode == SERIAL_MODE_NOT_OPENED)
		snd_uart16550_do_open(uart);
	uart->filemode |= SERIAL_MODE_INPUT_OPEN;
	uart->midi_input = substream;
	spin_unlock_irqrestore(&uart->open_lock, flags);
	return 0;
}

static int snd_uart16550_input_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&uart->open_lock, flags);
	uart->filemode &= ~SERIAL_MODE_INPUT_OPEN;
	uart->midi_input = NULL;
	if (uart->filemode == SERIAL_MODE_NOT_OPENED)
		snd_uart16550_do_close(uart);
	spin_unlock_irqrestore(&uart->open_lock, flags);
	return 0;
}

static void snd_uart16550_input_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return);

	spin_lock_irqsave(&uart->open_lock, flags);
	if (up) {
		uart->filemode |= SERIAL_MODE_INPUT_TRIGGERED;
	} else {
		uart->filemode &= ~SERIAL_MODE_INPUT_TRIGGERED;
	}
	spin_unlock_irqrestore(&uart->open_lock, flags);
}

static int snd_uart16550_output_open(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&uart->open_lock, flags);
	if (uart->filemode == SERIAL_MODE_NOT_OPENED)
		snd_uart16550_do_open(uart);
	uart->filemode |= SERIAL_MODE_OUTPUT_OPEN;
	uart->midi_output[substream->number] = substream;
	spin_unlock_irqrestore(&uart->open_lock, flags);
	return 0;
};

static int snd_uart16550_output_close(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return -ENXIO);

	spin_lock_irqsave(&uart->open_lock, flags);
	uart->filemode &= ~SERIAL_MODE_OUTPUT_OPEN;
	uart->midi_output[substream->number] = NULL;
	if (uart->filemode == SERIAL_MODE_NOT_OPENED)
		snd_uart16550_do_close(uart);
	spin_unlock_irqrestore(&uart->open_lock, flags);
	return 0;
};

inline static void snd_uart16550_write_buffer(snd_uart16550_t *uart, unsigned char byte)
{
	unsigned short buff_in = uart->buff_in;
	uart->tx_buff[buff_in] = byte;
	buff_in++;
	buff_in &= TX_BUFF_MASK;
	uart->buff_in = buff_in;
	uart->buff_in_count++;
	if (uart->irq < 0) /* polling mode */
		snd_uart16550_add_timer(uart); 
}

static void snd_uart16550_output_byte(snd_uart16550_t *uart, snd_rawmidi_substream_t * substream, unsigned char midi_byte)
{
	if (uart->buff_in_count == 0                            /* Buffer empty? */
	    && (uart->adaptor != SNDRV_SERIAL_MS124W_SA ||
		(uart->fifo_count == 0                               /* FIFO empty? */
		 && (inb(uart->base + UART_MSR) & UART_MSR_CTS)))) { /* CTS? */

	        /* Tx Buffer Empty - try to write immediately */
		if ((inb(uart->base + UART_LSR) & UART_LSR_THRE) != 0) {
		        /* Transmitter holding register (and Tx FIFO) empty */
		        uart->fifo_count = 1;
			outb(midi_byte, uart->base + UART_TX);
		} else {
		        if (uart->fifo_count < uart->fifo_limit) {
			        uart->fifo_count++;
				outb(midi_byte, uart->base + UART_TX);
			} else {
			        /* Cannot write (buffer empty) - put char in buffer */
				snd_uart16550_write_buffer(uart, midi_byte);
			}
		}
	} else {
		if (uart->buff_in_count >= TX_BUFF_SIZE) {
			snd_printk("%s: Buffer overrun on device at 0x%lx\n",
				   uart->rmidi->name, uart->base);
			return;
		}
		snd_uart16550_write_buffer(uart, midi_byte);
	}
}

static void snd_uart16550_output_write(snd_rawmidi_substream_t * substream)
{
	unsigned long flags;
	unsigned char midi_byte, addr_byte;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return);
	char first;
	
	/* Interupts are disabled during the updating of the tx_buff,
	 * since it is 'bad' to have two processes updating the same
	 * variables (ie buff_in & buff_out)
	 */

	spin_lock_irqsave(&uart->open_lock, flags);

	if (uart->irq < 0)	//polling
		snd_uart16550_io_loop(uart);

	if (uart->adaptor == SNDRV_SERIAL_MS124W_MB) {
		while (1) {
			/* buffer full? */
			/* in this mode we need two bytes of space */
			if (uart->buff_in_count > TX_BUFF_SIZE - 2)
				break;
			if (snd_rawmidi_transmit(substream, &midi_byte, 1) != 1)
				break;
#if SNDRV_SERIAL_MS124W_MB_NOCOMBO
			/* select exactly one of the four ports */
			addr_byte = (1 << (substream->number + 4)) | 0x08;
#else
			/* select any combination of the four ports */
			addr_byte = (substream->number << 4) | 0x08;
			/* ...except none */
			if (addr_byte == 0x08) addr_byte = 0xf8;
#endif
			snd_uart16550_output_byte(uart, substream, addr_byte);
			/* send midi byte */
			snd_uart16550_output_byte(uart, substream, midi_byte);
		}
	} else {
		first = 0;
		while (1) {
			/* buffer full? */
			if (uart->buff_in_count >= TX_BUFF_SIZE)
				break;
			if (snd_rawmidi_transmit(substream, &midi_byte, 1) != 1)
				break;
			if (first == 0 && uart->adaptor == SNDRV_SERIAL_SOUNDCANVAS &&
			    uart->prev_out != substream->number) {
				/* Roland Soundcanvas part selection */
				/* If this substream of the data is different previous
				   substream in this uart, send the change part event */
				uart->prev_out = substream->number;
				/* change part */
				snd_uart16550_output_byte(uart, substream, 0xf5);
				/* data */
				snd_uart16550_output_byte(uart, substream, uart->prev_out + 1);
				/* If midi_byte is a data byte, send the previous status byte */
				if (midi_byte < 0x80)
					snd_uart16550_output_byte(uart, substream, uart->prev_status[uart->prev_out]);
			}
			/* send midi byte */
			snd_uart16550_output_byte(uart, substream, midi_byte);
			if (midi_byte >= 0x80 && midi_byte < 0xf0)
				uart->prev_status[uart->prev_out] = midi_byte;
			first = 1;
		}
	}
	spin_unlock_irqrestore(&uart->open_lock, flags);
}

static void snd_uart16550_output_trigger(snd_rawmidi_substream_t * substream, int up)
{
	unsigned long flags;
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, substream->rmidi->private_data, return);

	spin_lock_irqsave(&uart->open_lock, flags);
	if (up) {
		uart->filemode |= SERIAL_MODE_OUTPUT_TRIGGERED;
	} else {
		uart->filemode &= ~SERIAL_MODE_OUTPUT_TRIGGERED;
	}
	spin_unlock_irqrestore(&uart->open_lock, flags);
	if (up)
		snd_uart16550_output_write(substream);
}

static snd_rawmidi_ops_t snd_uart16550_output =
{
	open:		snd_uart16550_output_open,
	close:		snd_uart16550_output_close,
	trigger:	snd_uart16550_output_trigger,
};

static snd_rawmidi_ops_t snd_uart16550_input =
{
	open:		snd_uart16550_input_open,
	close:		snd_uart16550_input_close,
	trigger:	snd_uart16550_input_trigger,
};

static int snd_uart16550_free(snd_uart16550_t *uart)
{
	if (uart->irq >= 0)
		free_irq(uart->irq, (void *)uart);
	if (uart->res_base) {
		release_resource(uart->res_base);
		kfree_nocheck(uart->res_base);
	}
	snd_magic_kfree(uart);
	return 0;
};

static int snd_uart16550_dev_free(snd_device_t *device)
{
	snd_uart16550_t *uart = snd_magic_cast(snd_uart16550_t, device->device_data, return -ENXIO);
	return snd_uart16550_free(uart);
}

static int __init snd_uart16550_create(snd_card_t * card,
				       unsigned long iobase,
				       int irq,
				       unsigned int speed,
				       unsigned int base,
				       int adaptor,
				       snd_uart16550_t **ruart)
{
	static snd_device_ops_t ops = {
		dev_free:       snd_uart16550_dev_free,
	};
	snd_uart16550_t *uart;
	int err;


	if ((uart = snd_magic_kcalloc(snd_uart16550_t, 0, GFP_KERNEL)) == NULL)
		return -ENOMEM;
	uart->adaptor = adaptor;
	uart->card = card;
	spin_lock_init(&uart->open_lock);
	uart->irq = -1;
	if ((uart->res_base = request_region(iobase, 8, "Serial MIDI")) == NULL) {
		snd_printk("unable to grab ports 0x%lx-0x%lx\n", iobase, iobase + 8 - 1);
		return -EBUSY;
	}
	uart->base = iobase;
	if (irq >= 0) {
		if (request_irq(irq, snd_uart16550_interrupt,
				SA_INTERRUPT, "Serial MIDI", (void *) uart)) {
			uart->irq = -1;
			snd_printk("irq %d busy. Using Polling.\n", irq);
		} else {
			uart->irq = irq;
		}
	}
	uart->divisor = base / speed;
	uart->speed = base / (unsigned int)uart->divisor;
	uart->speed_base = base;
	uart->prev_out = -1;
	memset(uart->prev_status, 0x80, sizeof(unsigned char) * SNDRV_SERIAL_MAX_OUTS);
	uart->buffer_timer.function = snd_uart16550_buffer_timer;
	uart->buffer_timer.data = (unsigned long)uart;
	uart->timer_running = 0;

	/* Register device */
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, uart, &ops)) < 0) {
		snd_uart16550_free(uart);
		return err;
	}

	switch (uart->adaptor) {
	case SNDRV_SERIAL_MS124W_SA:
	case SNDRV_SERIAL_MS124W_MB:
		/* MS-124W can draw power from RTS and DTR if they
		   are in opposite states. */ 
		outb(UART_MCR_RTS | (0&UART_MCR_DTR), uart->base + UART_MCR);
		break;
	case SNDRV_SERIAL_MS124T:
		/* MS-124T can draw power from RTS and/or DTR (preferably
		   both) if they are asserted. */
		outb(UART_MCR_RTS | UART_MCR_DTR, uart->base + UART_MCR);
		break;
	default:
		break;
	}

	if (ruart)
		*ruart = uart;

	return 0;
}

static int __init snd_uart16550_rmidi(snd_uart16550_t *uart, int device, int outs, snd_rawmidi_t **rmidi)
{
	snd_rawmidi_t *rrawmidi;
	int err;

	if ((err = snd_rawmidi_new(uart->card, "UART Serial MIDI", device, outs, 1, &rrawmidi)) < 0)
		return err;
	snd_rawmidi_set_ops(rrawmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_uart16550_input);
	snd_rawmidi_set_ops(rrawmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_uart16550_output);
	sprintf(rrawmidi->name, "uart16550 MIDI #%d", device);
	rrawmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT |
			       SNDRV_RAWMIDI_INFO_INPUT |
			       SNDRV_RAWMIDI_INFO_DUPLEX;
	rrawmidi->private_data = uart;
	if (rmidi)
		*rmidi = rrawmidi;
	return 0;
}

static int __init snd_serial_probe(int dev)
{
	snd_card_t *card;
	snd_uart16550_t *uart;
	int err;

	if (!snd_enable[dev])
		return -ENOENT;

	switch (snd_adaptor[dev]) {
	case SNDRV_SERIAL_SOUNDCANVAS:
		break;
	case SNDRV_SERIAL_MS124T:
	case SNDRV_SERIAL_MS124W_SA:
		snd_outs[dev] = 1;
		break;
	case SNDRV_SERIAL_MS124W_MB:
		snd_outs[dev] = 16;
		break;
	default:
		snd_printk("Adaptor type is out of range 0-%d (%d)\n",
			   SNDRV_SERIAL_MAX_ADAPTOR, snd_adaptor[dev]);
		return -ENODEV;
	}

	if (snd_outs[dev] < 1 || snd_outs[dev] > SNDRV_SERIAL_MAX_OUTS) {
		snd_printk("Count of outputs is out of range 1-%d (%d)\n",
			   SNDRV_SERIAL_MAX_OUTS, snd_outs[dev]);
		return -ENODEV;
	}

	card  = snd_card_new(snd_index[dev], snd_id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "Serial");
	strcpy(card->shortname, "Serial midi (uart16550A)");

	if ((err = snd_uart16550_detect(snd_port[dev])) <= 0) {
		snd_card_free(card);
		printk(KERN_ERR "no UART detected at 0x%lx\n", (long)snd_port[dev]);
		return err;
	}

	if ((err = snd_uart16550_create(card,
					snd_port[dev],
					snd_irq[dev],
					snd_speed[dev],
					snd_base[dev],
					snd_adaptor[dev],
					&uart)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_uart16550_rmidi(uart, 0, snd_outs[dev], &uart->rmidi)) < 0) {
		snd_card_free(card);
		return err;
	}

	sprintf(card->longname, "%s at 0x%lx, irq %d speed %d div %d outs %d adaptor %s",
		card->shortname,
		uart->base,
		uart->irq,
		uart->speed,
		(int)uart->divisor,
		snd_outs[dev],
		adaptor_names[uart->adaptor]);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_serial_cards[dev] = card;
	return 0;
}

static int __init alsa_card_serial_init(void)
{
	int dev = 0;
	int cards = 0;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (snd_serial_probe(dev) == 0)
			cards++;
	}

	if (cards == 0) {
#ifdef MODULE
		printk(KERN_ERR "serial midi soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_serial_exit(void)
{
	int dev;

	for (dev = 0; dev < SNDRV_CARDS; dev++) {
		if (snd_serial_cards[dev] != NULL)
			snd_card_free(snd_serial_cards[dev]);
	}
}

module_init(alsa_card_serial_init)
module_exit(alsa_card_serial_exit)

#ifndef MODULE

/* format is: snd-serial=snd_enable,snd_index,snd_id,
			 snd_port,snd_irq,snd_speed,snd_base,snd_outs */

static int __init alsa_card_serial_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&snd_enable[nr_dev]) == 2 &&
	       get_option(&str,&snd_index[nr_dev]) == 2 &&
	       get_id(&str,&snd_id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&snd_port[nr_dev]) == 2 &&
	       get_option(&str,&snd_irq[nr_dev]) == 2 &&
	       get_option(&str,&snd_speed[nr_dev]) == 2 &&
	       get_option(&str,&snd_base[nr_dev]) == 2 &&
	       get_option(&str,&snd_outs[nr_dev]) == 2 &&
	       get_option(&str,&snd_adaptor[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-serial=", alsa_card_serial_setup);

#endif /* ifndef MODULE */
