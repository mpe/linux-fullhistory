extern void hp300_init_IRQ(void);
extern void (*hp300_handlers[8])(int, void *, struct pt_regs *);
extern void hp300_free_irq(unsigned int irq, void *dev_id);
extern int hp300_request_irq(unsigned int irq,
		void (*handler) (int, void *, struct pt_regs *),
		unsigned long flags, const char *devname, void *dev_id);
