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
# intermediate objects that form part of a module
unexport MI_OBJS
unexport ALL_MOBJS
# objects that export symbol tables
unexport OX_OBJS
unexport LX_OBJS
unexport MX_OBJS
unexport MIX_OBJS
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
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -S $< -o $@

%.i: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -E $< > $@

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -c -o $@ $<
	@ ( \
	    echo 'ifeq ($(strip $(subst $(comma),:,$(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@))),$$(strip $$(subst $$(comma),:,$$(CFLAGS) $$(EXTRA_CFLAGS) $$(CFLAGS_$@))))' ; \
	    echo 'FILES_FLAGS_UP_TO_DATE += $@' ; \
	    echo 'endif' \
	) > $(dir $@)/.$(notdir $@).flags

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
$(O_TARGET): $(ALL_O)
	rm -f $@
ifneq "$(strip $(ALL_O))" ""
	$(LD) $(EXTRA_LDFLAGS) -r -o $@ $(ALL_O)
else
	$(AR) rcs $@
endif
	@ ( \
	    echo 'ifeq ($(strip $(subst $(comma),:,$(EXTRA_LDFLAGS) $(ALL_O))),$$(strip $$(subst $$(comma),:,$$(EXTRA_LDFLAGS) $$(ALL_O))))' ; \
	    echo 'FILES_FLAGS_UP_TO_DATE += $@' ; \
	    echo 'endif' \
	) > $(dir $@)/.$(notdir $@).flags
endif # O_TARGET

#
# Rule to compile a set of .o files into one .a file
#
ifdef L_TARGET
$(L_TARGET): $(LX_OBJS) $(L_OBJS)
	rm -f $@
	$(AR) $(EXTRA_ARFLAGS) rcs $@ $(LX_OBJS) $(L_OBJS)
	@ ( \
	    echo 'ifeq ($(strip $(subst $(comma),:,$(EXTRA_ARFLAGS) $(LX_OBJS) $(L_OBJS))),$$(strip $$(subst $$(comma),:,$$(EXTRA_ARFLAGS) $$(LX_OBJS) $$(L_OBJS))))' ; \
	    echo 'FILES_FLAGS_UP_TO_DATE += $@' ; \
	    echo 'endif' \
	) > $(dir $@)/.$(notdir $@).flags
endif

#
# This make dependencies quickly
#
fastdep: dummy
	$(TOPDIR)/scripts/mkdep $(wildcard *.[chS] local.h.master) > .depend
ifdef ALL_SUB_DIRS
	$(MAKE) $(patsubst %,_sfdep_%,$(ALL_SUB_DIRS)) _FASTDEP_ALL_SUB_DIRS="$(ALL_SUB_DIRS)"
endif

ifdef _FASTDEP_ALL_SUB_DIRS
$(patsubst %,_sfdep_%,$(_FASTDEP_ALL_SUB_DIRS)):
	$(MAKE) -C $(patsubst _sfdep_%,%,$@) fastdep
endif

	
#
# A rule to make subdirectories
#
sub_dirs: dummy $(patsubst %,_subdir_%,$(SUB_DIRS))

ifdef SUB_DIRS
$(patsubst %,_subdir_%,$(SUB_DIRS)) : dummy
	$(MAKE) -C $(patsubst _subdir_%,%,$@)
endif

#
# A rule to make modules
#
ALL_MOBJS = $(MX_OBJS) $(M_OBJS)
ifneq "$(strip $(ALL_MOBJS))" ""
PDWN=$(shell $(CONFIG_SHELL) $(TOPDIR)/scripts/pathdown.sh)
endif

ifdef MOD_SUB_DIRS
$(patsubst %,_modsubdir_%,$(MOD_SUB_DIRS)) : dummy
	$(MAKE) -C $(patsubst _modsubdir_%,%,$@) modules
endif

ifdef MOD_IN_SUB_DIRS
$(patsubst %,_modinsubdir_%,$(MOD_IN_SUB_DIRS)) : dummy
	$(MAKE) -C $(patsubst _modinsubdir_%,%,$@) modules
endif

modules: $(ALL_MOBJS) $(MIX_OBJS) $(MI_OBJS) dummy \
	 $(patsubst %,_modsubdir_%,$(MOD_SUB_DIRS)) \
	 $(patsubst %,_modinsubdir_%,$(MOD_IN_SUB_DIRS))
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
	    ln -sf ../$(PDWN)/$$i $$i; done
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

SYMTAB_OBJS = $(LX_OBJS) $(OX_OBJS) $(MX_OBJS) $(MIX_OBJS)

ifdef CONFIG_MODVERSIONS
ifneq "$(strip $(SYMTAB_OBJS))" ""

MODINCL = $(TOPDIR)/include/linux/modules

# The -w option (enable warnings) for genksyms will return here in 2.1
# So where has it gone?
#
# Added the SMP separator to stop module accidents between uniprocessor
# and SMP Intel boxes - AC - from bits by Michael Chastain
#

ifdef CONFIG_SMP
	genksyms_smp_prefix := -p smp_
else
	genksyms_smp_prefix := 
endif

$(MODINCL)/%.ver: %.c
	@if [ ! -r $(MODINCL)/$*.stamp -o $(MODINCL)/$*.stamp -ot $< ]; then \
		echo '$(CC) $(CFLAGS) -E -D__GENKSYMS__ $<'; \
		echo '| $(GENKSYMS) $(genksyms_smp_prefix) -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp'; \
		$(CC) $(CFLAGS) -E -D__GENKSYMS__ $< \
		| $(GENKSYMS) $(genksyms_smp_prefix) -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp; \
		if [ -r $@ ] && cmp -s $@ $@.tmp; then echo $@ is unchanged; rm -f $@.tmp; \
		else echo mv $@.tmp $@; mv -f $@.tmp $@; fi; \
	fi; touch $(MODINCL)/$*.stamp
	
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
$(SYMTAB_OBJS): $(TOPDIR)/include/linux/modversions.h $(SYMTAB_OBJS:.o=.c)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -DEXPORT_SYMTAB -c $(@:.o=.c)
	@ ( \
	    echo 'ifeq ($(strip $(subst $(comma),:,$(CFLAGS) $(EXTRA_CFLAGS) $(CFLAGS_$@) -DEXPORT_SYMTAB)),$$(strip $$(subst $$(comma),:,$$(CFLAGS) $$(EXTRA_CFLAGS) $$(CFLAGS_$@) -DEXPORT_SYMTAB)))' ; \
	    echo 'FILES_FLAGS_UP_TO_DATE += $@' ; \
	    echo 'endif' \
	) > $(dir $@)/.$(notdir $@).flags
endif

endif # CONFIG_MODULES


#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif

ifneq ($(wildcard $(TOPDIR)/.hdepend),)
include $(TOPDIR)/.hdepend
endif

#
# Find files whose flags have changed and force recompilation.
# For safety, this works in the converse direction:
#   every file is forced, except those whose flags are positively up-to-date.
#
FILES_FLAGS_UP_TO_DATE :=

# For use in expunging commas from flags, which mung our checking.
comma = ,

FILES_FLAGS_EXIST := $(wildcard .*.flags)
ifneq ($(FILES_FLAGS_EXIST),)
include $(FILES_FLAGS_EXIST)
endif

FILES_FLAGS_CHANGED := $(strip \
    $(filter-out $(FILES_FLAGS_UP_TO_DATE), \
	$(O_TARGET) $(O_OBJS) $(OX_OBJS) \
	$(L_TARGET) $(L_OBJS) $(LX_OBJS) \
	$(M_OBJS) $(MX_OBJS) \
	$(MI_OBJS) $(MIX_OBJS) \
	))

# A kludge: .S files don't get flag dependencies (yet),
#   because that will involve changing a lot of Makefiles.
FILES_FLAGS_CHANGED := $(strip \
    $(filter-out $(patsubst %.S, %.o, $(wildcard *.S)), \
    $(FILES_FLAGS_CHANGED)))

ifneq ($(FILES_FLAGS_CHANGED),)
$(FILES_FLAGS_CHANGED): dummy
endif
