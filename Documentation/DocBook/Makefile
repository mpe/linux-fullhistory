BOOKS	:= wanbook.sgml z8530book.sgml mcabook.sgml videobook.sgml \
	   kernel-api.sgml parportbook.sgml kernel-hacking.sgml \
	   kernel-locking.sgml via-audio.sgml mousedrivers.sgml

PS	:=	$(patsubst %.sgml, %.ps, $(BOOKS))
PDF	:=	$(patsubst %.sgml, %.pdf, $(BOOKS))
HTML    :=      $(patsubst %.sgml, %, $(BOOKS))
IMG-parportbook := parport-share.fig parport-multi.fig parport-structure.fig
EPS-parportbook := $(patsubst %.fig, %.eps, $(IMG-parportbook))
JPG-parportbook := $(patsubst %.fig, %.jpeg, $(IMG-parportbook))

$(BOOKS): $(TOPDIR)/scripts/docproc

.PHONY:	books ps pdf html clean mrproper db2ps db2pdf db2html

books:	$(BOOKS)

ps:	$(PS)

pdf:	$(PDF)

html:	$(HTML)

db2ps db2pdf db2html:
	@(which $@ > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)

%.eps: %.fig
	-fig2dev -Leps $< $@

%.jpeg: %.fig
	-fig2dev -Ljpeg $< $@

$(TOPDIR)/scripts/docproc:
	$(MAKE) -C $(TOPDIR)/scripts docproc

mousedrivers.sgml: mousedrivers.tmpl
	$(TOPDIR)/scripts/docgen <$< >$@

kernel-hacking.sgml: kernel-hacking.tmpl
	$(TOPDIR)/scripts/docgen <$< >$@

kernel-locking.sgml: kernel-locking.tmpl
	$(TOPDIR)/scripts/docgen <$< >$@

wanbook.sgml: wanbook.tmpl $(TOPDIR)/drivers/net/wan/syncppp.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<wanbook.tmpl >wanbook.sgml

z8530book.sgml: z8530book.tmpl $(TOPDIR)/drivers/net/wan/z85230.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/z85230.c \
		<z8530book.tmpl >z8530book.sgml

via-audio.sgml: via-audio.tmpl $(TOPDIR)/drivers/sound/via82cxxx_audio.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/sound/via82cxxx_audio.c \
		<via-audio.tmpl >via-audio.sgml

mcabook.sgml: mcabook.tmpl $(TOPDIR)/arch/i386/kernel/mca.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/arch/i386/kernel/mca.c \
		<mcabook.tmpl >mcabook.sgml

videobook.sgml: videobook.tmpl $(TOPDIR)/drivers/char/videodev.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/char/videodev.c \
		<videobook.tmpl >videobook.sgml

APISOURCES :=	$(TOPDIR)/drivers/char/videodev.c \
		$(TOPDIR)/arch/i386/kernel/mca.c \
		$(TOPDIR)/arch/i386/kernel/mtrr.c \
		$(TOPDIR)/drivers/char/misc.c \
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
		$(TOPDIR)/net/netsyms.c
 
kernel-api.sgml: kernel-api.tmpl $(APISOURCES)
	$(TOPDIR)/scripts/docgen $(APISOURCES) \
		<kernel-api.tmpl >kernel-api.sgml

parportbook: $(JPG-parportbook)
parportbook.ps: $(EPS-parportbook)
parportbook.sgml: parportbook.tmpl $(TOPDIR)/drivers/parport/init.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/parport/init.c \
		<parportbook.tmpl >parportbook.sgml

DVI	:=	$(patsubst %.sgml, %.dvi, $(BOOKS))
AUX	:=	$(patsubst %.sgml, %.aux, $(BOOKS))
TEX	:=	$(patsubst %.sgml, %.tex, $(BOOKS))
LOG	:=	$(patsubst %.sgml, %.log, $(BOOKS))
OUT	:=	$(patsubst %.sgml, %.out, $(BOOKS))

clean:
	-$(RM) core *~
	-$(RM) $(BOOKS)
	-$(RM) $(DVI) $(AUX) $(TEX) $(LOG) $(OUT)
	-$(RM) $(JPG-parportbook) $(EPS-parportbook)

mrproper: clean
	-$(RM) $(PS) $(PDF)
	-$(RM) -r $(HTML)

%.ps : %.sgml db2ps
	db2ps $<

%.pdf : %.sgml db2pdf
	db2pdf $<

%:	%.sgml db2html
	-$(RM) -r $@
	db2html $<
	if [ ! -z "$(JPG-$@)" ]; then cp $(JPG-$@) $@; fi

include $(TOPDIR)/Rules.make

