ifdef CONFIG_MODVERSIONS
TOPINCL := $(TOPDIR)/include/linux

# Uses SYMTAB_OBJS
# Separate the object into "normal" objects and "exporting" objects
# Exporting objects are: all objects that define symbol tables
#
# Add dependence on $(SYMTAB_OBJS) to the main target
#

.SUFFIXES: .ver

.c.ver:
	$(CC) $(CFLAGS) -E -D__GENKSYMS__ $< | genksyms -w $(TOPINCL)/modules
	@ln -sf $(TOPINCL)/modules/$@ .

$(SYMTAB_OBJS):
	$(CC) $(CFLAGS) -DEXPORT_SYMTAB -c $(@:.o=.c)

$(TOPINCL)/modversions.h: $(SYMTAB_OBJS:.o=.ver)
	@echo updating $(TOPINCL)/modversions.h
	@(cd $(TOPINCL)/modules; for f in *.ver;\
	do echo "#include <linux/modules/$${f}>"; done) \
	> $(TOPINCL)/modversions.h

dep: $(TOPINCL)/modversions.h

endif
