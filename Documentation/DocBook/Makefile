BOOKS	:= wanbook.sgml z8530book.sgml mcabook.sgml videobook.sgml kernel-api.sgml
	
books:	docproc $(BOOKS)

docproc:
	$(MAKE) -C $(TOPDIR)/scripts docproc

wanbook.sgml: wanbook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<wanbook.tmpl >wanbook.sgml

z8530book.sgml: z8530book.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/z85230.c \
		<z8530book.tmpl >z8530book.sgml

mcabook.sgml: mcabook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/arch/i386/kernel/mca.c \
		<mcabook.tmpl >mcabook.sgml

videobook.sgml: videobook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/char/videodev.c \
		<videobook.tmpl >videobook.sgml

kernel-api.sgml: kernel-api.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/char/videodev.c \
		$(TOPDIR)/arch/i386/kernel/mca.c \
		$(TOPDIR)/arch/i386/kernel/mtrr.c \
		$(TOPDIR)/drivers/char/misc.c \
		$(TOPDIR)/drivers/char/videodev.c \
		$(TOPDIR)/drivers/net/net_init.c \
		$(TOPDIR)/drivers/net/8390.c \
		$(TOPDIR)/drivers/char/serial.c \
		$(TOPDIR)/drivers/pci/pci.c \
		$(TOPDIR)/drivers/sound/sound_core.c \
		$(TOPDIR)/drivers/sound/sound_firmware.c \
		$(TOPDIR)/drivers/net/wan/syncppp.c \
		$(TOPDIR)/drivers/net/wan/z85230.c \
		$(TOPDIR)/kernel/pm.c \
		<kernel-api.tmpl >kernel-api.sgml

clean:
	rm -f core *~
	rm -f $(BOOKS)

include $(TOPDIR)/Rules.make

