#
# This file contains rules which are shared between multiple Makefiles.
#

#
# False targets.
#
.PHONY: dummy

#
# Special variables which should not be exported
#
unexport EXTRA_ASFLAGS
unexport EXTRA_CFLAGS
unexport EXTRA_LDFLAGS
unexport EXTRA_ARFLAGS
unexport SUBDIRS
unexport SUB_DIRS
unexport ALL_SUB_DIRS
unexport MOD_SUB_DIRS
unexport O_TARGET
unexport O_OBJS
unexport L_OBJS
unexport M_OBJS
unexport ALL_MOBJS
# objects that export symbol tables
unexport OX_OBJS
unexport LX_OBJS
unexport MX_OBJS
unexport SYMTAB_OBJS

unexport MOD_LIST_NAME

#
# Get things started.
#
first_rule: sub_dirs
	$(MAKE) all_targets

#
# Common rules
#

%.s: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -S $< -o $@

%.i: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -E $< > $@

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

%.o: %.s
	$(AS) $(ASFLAGS) $(EXTRA_CFLAGS) -o $@ $<

#
#
#
all_targets: $(O_TARGET) $(L_TARGET)

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
ALL_O = $(OX_OBJS) $(O_OBJS)
$(O_TARGET): $(ALL_O) $(TOPDIR)/include/linux/config.h
	rm -f $@
ifneq "$(strip $(ALL_O))" ""
	$(LD) $(EXTRA_LDFLAGS) -r -o $@ $(ALL_O)
else
	$(AR) rcs $@
endif
endif # O_TARGET

#
# Rule to compile a set of .o files into one .a file
#
ifdef L_TARGET
$(L_TARGET): $(LX_OBJS) $(L_OBJS) $(TOPDIR)/include/linux/config.h
	rm -f $@
	$(AR) $(EXTRA_ARFLAGS) rcs $@ $(LX_OBJS) $(L_OBJS)
endif

#
# This make dependencies quickly
#
fastdep: dummy
	$(TOPDIR)/scripts/mkdep *.[chS] > .depend
ifdef ALL_SUB_DIRS
	set -e; for i in $(ALL_SUB_DIRS); do $(MAKE) -C $$i fastdep; done
endif

#
# A rule to make subdirectories
#
sub_dirs: dummy
ifdef SUB_DIRS
	set -e; for i in $(SUB_DIRS); do $(MAKE) -C $$i; done
endif

#
# A rule to make modules
#
ALL_MOBJS = $(MX_OBJS) $(M_OBJS)
ifneq "$(strip $(ALL_MOBJS))" ""
PDWN=$(shell $(CONFIG_SHELL) $(TOPDIR)/scripts/pathdown.sh)
endif
modules: $(ALL_MOBJS) dummy
ifdef MOD_SUB_DIRS
	set -e; for i in $(MOD_SUB_DIRS); do $(MAKE) -C $$i modules; done
endif
ifdef MOD_IN_SUB_DIRS
	set -e; for i in $(MOD_IN_SUB_DIRS); do $(MAKE) -C $$i modules; done
endif
ifneq "$(strip $(MOD_LIST_NAME))" ""
	rm -f $$TOPDIR/modules/$(MOD_LIST_NAME)
ifdef MOD_SUB_DIRS
	for i in $(MOD_SUB_DIRS); do \
	    echo `basename $$i`.o >> $$TOPDIR/modules/$(MOD_LIST_NAME); done
endif
ifneq "$(strip $(ALL_MOBJS))" ""
	echo $(ALL_MOBJS) >> $$TOPDIR/modules/$(MOD_LIST_NAME)
endif
ifneq "$(strip $(MOD_TO_LIST))" ""
	echo $(MOD_TO_LIST) >> $$TOPDIR/modules/$(MOD_LIST_NAME)
endif
endif
ifneq "$(strip $(ALL_MOBJS))" ""
	echo $(PDWN)
	cd $$TOPDIR/modules; for i in $(ALL_MOBJS); do \
	    ln -sf ../$(PDWN)/$$i .; done
endif

#
# A rule to do nothing
#
dummy:

#
# This is useful for testing
#
script:
	$(SCRIPT)

#
# This sets version suffixes on exported symbols
# Uses SYMTAB_OBJS
# Separate the object into "normal" objects and "exporting" objects
# Exporting objects are: all objects that define symbol tables
#
ifdef CONFIG_MODULES

SYMTAB_OBJS = $(LX_OBJS) $(OX_OBJS) $(MX_OBJS)

ifdef CONFIG_MODVERSIONS
ifneq "$(strip $(SYMTAB_OBJS))" ""

MODINCL = $(TOPDIR)/include/linux/modules

# The -w option (enable warnings) for genksyms will return here in 2.1
$(MODINCL)/%.ver: %.c
	$(CC) $(CFLAGS) -E -D__GENKSYMS__ $<\
	| $(GENKSYMS) -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp
	mv $@.tmp $@
	
$(addprefix $(MODINCL)/,$(SYMTAB_OBJS:.o=.ver)): $(TOPDIR)/include/linux/autoconf.h

$(TOPDIR)/include/linux/modversions.h: $(addprefix $(MODINCL)/,$(SYMTAB_OBJS:.o=.ver))
	@echo updating $(TOPDIR)/include/linux/modversions.h
	@(echo "#ifndef _LINUX_MODVERSIONS_H";\
	  echo "#define _LINUX_MODVERSIONS_H"; \
	  echo "#include <linux/modsetver.h>"; \
	  cd $(TOPDIR)/include/linux/modules; \
	  for f in *.ver; do \
	    if [ -f $$f ]; then echo "#include <linux/modules/$${f}>"; fi; \
	  done; \
	  echo "#endif"; \
	) > $@

dep fastdep: $(TOPDIR)/include/linux/modversions.h

endif # SYMTAB_OBJS 

$(M_OBJS): $(TOPDIR)/include/linux/modversions.h
ifdef MAKING_MODULES
$(O_OBJS) $(L_OBJS): $(TOPDIR)/include/linux/modversions.h
endif

else

$(TOPDIR)/include/linux/modversions.h:
	@echo "#include <linux/modsetver.h>" > $@

endif # CONFIG_MODVERSIONS

ifneq "$(strip $(SYMTAB_OBJS))" ""
$(SYMTAB_OBJS): $(TOPDIR)/include/linux/modversions.h
	$(CC) $(CFLAGS) -DEXPORT_SYMTAB -c $(@:.o=.c)
endif

endif # CONFIG_MODULES


#
# include dependency files they exist
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

ifeq ($(TOPDIR)/.hdepend,$(wildcard $(TOPDIR)/.hdepend))
include $(TOPDIR)/.hdepend
endif
