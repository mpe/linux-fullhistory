BOOKS	:= wanbook.sgml z8530book.sgml mcabook.sgml videobook.sgml kernel-api.sgml parportbook.sgml kernel-hacking.sgml kernel-locking.sgml

PS	:=	$(patsubst %.sgml, %.ps, $(BOOKS))
PDF	:=	$(patsubst %.sgml, %.pdf, $(BOOKS))

$(BOOKS): $(TOPDIR)/scripts/docproc

.PHONY:	books ps pdf clean mrproper db2ps db2pdf

books:	$(BOOKS)

ps:	$(PS)

pdf:	$(PDF)

db2ps db2pdf:
	@(which $@ > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)

%.eps: %.fig
	-fig2dev -Leps $< $@

$(TOPDIR)/scripts/docproc:
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
		$(TOPDIR)/fs/devfs/base.c \
		$(TOPDIR)/kernel/pm.c \
		$(TOPDIR)/kernel/ksyms.c \
		$(TOPDIR)/net/netsyms.c \
		<kernel-api.tmpl >kernel-api.sgml

parportbook.sgml: parportbook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/parport/init.c \
		<parportbook.tmpl >parportbook.sgml

DVI	:=	$(patsubst %.sgml, %.dvi, $(BOOKS))
AUX	:=	$(patsubst %.sgml, %.aux, $(BOOKS))
TEX	:=	$(patsubst %.sgml, %.tex, $(BOOKS))
LOG	:=	$(patsubst %.sgml, %.log, $(BOOKS))

clean:
	-$(RM) core *~
	-$(RM) $(BOOKS)
	-$(RM) $(DVI) $(AUX) $(TEX) $(LOG)
	-$(RM) parport-share.eps parport-multi.eps parport-structure.eps

mrproper: clean
	-$(RM) $(PS) $(PDF)

parportbook.ps: parport-share.eps parport-multi.eps parport-structure.eps

%.ps : %.sgml db2ps
	db2ps $<

%.pdf : %.sgml
	db2pdf $<

include $(TOPDIR)/Rules.make

