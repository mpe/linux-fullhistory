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
unexport MOD_LIST_NAME

#
# Get things started.
#
first_rule: sub_dirs $(O_TARGET) $(L_TARGET)

#
# Common rules
#
.c.s:
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -S $< -o $@

.c.o:
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

.s.o:
	$(AS) $(ASFLAGS) $(EXTRA_CFLAGS) -o $@ $<

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
$(O_TARGET): $(O_OBJS) $(TOPDIR)/include/linux/config.h
	rm -f $@
ifdef O_OBJS
	$(LD) $(EXTRA_LDFLAGS) -r -o $@ $(O_OBJS)
else
	$(AR) rcs $@
endif
endif

#
# Rule to compile a set of .o files into one .a file
#
ifdef L_TARGET
$(L_TARGET): $(L_OBJS) $(TOPDIR)/include/linux/config.h
	rm -f $@
	$(AR) $(EXTRA_ARFLAGS) rcs $@ $(L_OBJS)
endif

#
# This make dependencies quickly
#
fastdep: dummy
	if [ -n "$(wildcard *.[chS])" ]; then \
	awk -f $(TOPDIR)/scripts/depend.awk *.[chS] > .depend; fi
	set -e; for i in $(ALL_SUB_DIRS); do $(MAKE) -C $$i fastdep; done

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
ifdef M_OBJS
PDWN=$(shell /bin/sh $(TOPDIR)/scripts/pathdown.sh)
endif
modules: $(M_OBJS) dummy
ifdef MOD_SUB_DIRS
	set -e; for i in $(MOD_SUB_DIRS); do $(MAKE) -C $$i modules; done
endif
ifneq "$(strip $(MOD_LIST_NAME))" ""
ifndef M_OBJS # Hack for fs subdirectories
	rm -f $$TOPDIR/modules/$(MOD_LIST_NAME)
	for i in $(MOD_SUB_DIRS); do \
	    echo `basename $$i`.o >> $$TOPDIR/modules/$(MOD_LIST_NAME); done
else
	echo $(M_OBJS) > $$TOPDIR/modules/$(MOD_LIST_NAME)
endif
endif
ifdef M_OBJS
	echo $(PDWN)
	cd $$TOPDIR/modules; for i in $(M_OBJS); do \
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
# include dependency files they exist
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

ifeq ($(TOPDIR)/.hdepend,$(wildcard $(TOPDIR)/.hdepend))
include $(TOPDIR)/.hdepend
endif
