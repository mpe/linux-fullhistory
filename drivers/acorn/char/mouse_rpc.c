/*
 * linux/drivers/char/rpcmouse.c
 *
 * Copyright (C) 1996-1998 Russell King
 *
 * This handles the Acorn RiscPCs mouse.  We basically have a couple
 * of hardware registers that track the sensor count for the X-Y movement
 * and another register holding the button state.  On every VSYNC interrupt
 * we read the complete state and then work out if something has changed.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "../../char/mouse.h"

static short old_x, old_y, old_b;
static int mousedev;

void
mouse_rpc_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	short x, y, dx, dy;
	int buttons;

	x = (short)inl(IOMD_MOUSEX);
	y = (short)inl(IOMD_MOUSEY);
	buttons = (inl (0x800C4000) >> 4) & 7;

	dx = x - old_x;
	old_x = x;
	dy = y - old_y;
	old_y = y;

	if (dx || dy || buttons != old_b) {
		busmouse_add_movementbuttons(mousedev, dx, dy, buttons);
		old_b = buttons;
	}
}

static struct busmouse rpcmouse = {
	6, "arcmouse", NULL, NULL, 7
};

int
mouse_rpc_init(void)
{
	mousedev = register_busmouse(&rpcmouse);

	if (mousedev < 0)
		printk("rpcmouse: could not register mouse driver\n");
	else {
		old_x = (short)inl(IOMD_MOUSEX);
		old_y = (short)inl(IOMD_MOUSEY);
		old_b = (inl (0x800C4000) >> 4) & 7;
		if (request_irq(IRQ_VSYNCPULSE, mouse_rpc_irq, SA_SHIRQ, "mouse", &mousedev)) {
			printk("rpcmouse: unable to allocate VSYNC interrupt\n");
			unregister_busmouse(mousedev);
			mousedev = -1;
		}
	}

	return mousedev >= 0 ? 0 : -ENODEV;
}

#ifdef MODULE
int
init_module(void)
{
	return mouse_rpc_init();
}

int
cleanup_module(void)
{
	if (mousedev >= 0) {
		unregister_busmouse(mousedev);
		free_irq(IRQ_VSYNCPULSE, &mousedev);
	}
}
#endif
