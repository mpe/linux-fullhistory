BOOKS	:= wanbook.sgml z8530book.sgml mcabook.sgml videobook.sgml
	
books:	docproc $(BOOKS)

docproc:
	$(MAKE) -C $(TOPDIR)/scripts docproc

wanbook.sgml: wanbook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<wanbook.tmpl >wanbook.sgml

z8530book.sgml: z8530book.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<z8530book.tmpl >z8530book.sgml

mcabook.sgml: mcabook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/arch/i386/kernel/mca.c \
		<mcabook.tmpl >mcabook.sgml

videobook.sgml: videobook.tmpl
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/char/videodev.c \
		<videobook.tmpl >videobook.sgml

clean:
	rm -f core *~
	rm -r$BOOKS

include $(TOPDIR)/Rules.make

