BOOKS	:= wanbook.sgml z8530book.sgml
	
books:	docproc $(BOOKS)

docproc:
	$(MAKE) -C $(TOPDIR)/scripts docproc

wanbook.sgml:
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<wanbook.tmpl >wanbook.sgml

z8530book.sgml:
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<z8530book.tmpl >z8530book.sgml

clean:
	rm -f core *~
	rm -r$BOOKS

include $(TOPDIR)/Rules.make

