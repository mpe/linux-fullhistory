#
# This file contains rules which are shared between multiple Makefiles.
#

#
# Special variables which should not be exported
#
unexport EXTRA_AFLAGS
unexport EXTRA_CFLAGS
unexport EXTRA_LDFLAGS
unexport EXTRA_ARFLAGS
unexport SUBDIRS
unexport SUB_DIRS
unexport ALL_SUB_DIRS
unexport MOD_SUB_DIRS
unexport O_TARGET

unexport obj-y
unexport obj-m
unexport obj-n
unexport obj-
unexport export-objs
unexport subdir-y
unexport subdir-m
unexport subdir-n
unexport subdir-
unexport mod-subdirs

# Some standard vars

comma   := ,
empty   :=
space   := $(empty) $(empty)

# Figure out paths
# ---------------------------------------------------------------------------
# Find the path relative to the toplevel dir, $(RELDIR), and express
# the toplevel dir as a relative path from this dir, $(TOPDIR_REL)

ifeq ($(findstring $(TOPDIR),$(CURDIR)),)
  # Can only happen when something is built out of tree
  RELDIR := $(CURDIR)
  TOPDIR_REL := $(TOPDIR)
else
  RELDIR := $(subst $(TOPDIR)/,,$(CURDIR))
  TOPDIR_REL := $(subst $(space),,$(foreach d,$(subst /, ,$(RELDIR)),../))
endif

# Figure out what we need to build from the various variables
# ===========================================================================

# When an object is listed to be built compiled-in and modular,
# only build the compiled-in version

obj-m := $(filter-out $(obj-y),$(obj-m))

# Handle objects in subdirs
# ---------------------------------------------------------------------------
# o if we encounter foo/ in $(obj-y), replace it by foo/built-in.o
#   and add the directory to the list of dirs to descend into: $(subdir-y)
# o if we encounter foo/ in $(obj-m), remove it from $(obj-m) 
#   and add the directory to the list of dirs to descend into: $(subdir-m)

__subdir-y	:= $(patsubst %/,%,$(filter %/, $(obj-y)))
subdir-y	+= $(__subdir-y)
__subdir-m	:= $(patsubst %/,%,$(filter %/, $(obj-m)))
subdir-m	+= $(__subdir-m)
__subdir-n	:= $(patsubst %/,%,$(filter %/, $(obj-n)))
subdir-n	+= $(__subdir-n)
__subdir-	:= $(patsubst %/,%,$(filter %/, $(obj-)))
subdir-		+= $(__subdir-)
obj-y		:= $(patsubst %/, %/built-in.o, $(obj-y))
obj-m		:= $(filter-out %/, $(obj-m))

# If a dir is selected in $(subdir-y) and also mentioned in $(mod-subdirs),
# add it to $(subdir-m)

both-m          := $(filter $(mod-subdirs), $(subdir-y))
SUB_DIRS	:= $(subdir-y)
MOD_SUB_DIRS	:= $(sort $(subdir-m) $(both-m))
ALL_SUB_DIRS	:= $(sort $(subdir-y) $(subdir-m) $(subdir-n) $(subdir-))

# export.o is never a composite object, since $(export-objs) has a
# fixed meaning (== objects which EXPORT_SYMBOL())
__obj-y = $(filter-out export.o,$(obj-y))
__obj-m = $(filter-out export.o,$(obj-m))

# if $(foo-objs) exists, foo.o is a composite object 
__multi-used-y := $(sort $(foreach m,$(__obj-y), $(if $($(m:.o=-objs)), $(m))))
__multi-used-m := $(sort $(foreach m,$(__obj-m), $(if $($(m:.o=-objs)), $(m))))

# FIXME: Rip this out later
# Backwards compatibility: if a composite object is listed in
# $(list-multi), skip it here, since the Makefile will have an explicit
# link rule for it

multi-used-y := $(filter-out $(list-multi),$(__multi-used-y))
multi-used-m := $(filter-out $(list-multi),$(__multi-used-m))

# Build list of the parts of our composite objects, our composite
# objects depend on those (obviously)
multi-objs-y := $(foreach m, $(multi-used-y), $($(m:.o=-objs)))
multi-objs-m := $(foreach m, $(multi-used-m), $($(m:.o=-objs)))

# $(subdir-obj-y) is the list of objects in $(obj-y) which do not live
# in the local directory
subdir-obj-y := $(foreach o,$(obj-y),$(if $(filter-out $(o),$(notdir $(o))),$(o)))

# Replace multi-part objects by their individual parts, look at local dir only
real-objs-y := $(foreach m, $(filter-out $(subdir-obj-y), $(obj-y)), $(if $($(m:.o=-objs)),$($(m:.o=-objs)),$(m)))
real-objs-m := $(foreach m, $(obj-m), $(if $($(m:.o=-objs)),$($(m:.o=-objs)),$(m)))

# ==========================================================================
#
# Get things started.
#
first_rule: all_targets

#
# Common rules
#

# Compile C sources (.c)
# ---------------------------------------------------------------------------

# FIXME: if we don't know if built-in or modular, assume built-in.
# Only happens in Makefiles which override the default first_rule:
modkern_cflags := $(CFLAGS_KERNEL)

$(real-objs-y)      : modkern_cflags := $(CFLAGS_KERNEL)
$(real-objs-y:.o=.i): modkern_cflags := $(CFLAGS_KERNEL)
$(real-objs-y:.o=.s): modkern_cflags := $(CFLAGS_KERNEL)

$(real-objs-m)      : modkern_cflags := $(CFLAGS_MODULE)
$(real-objs-m:.o=.i): modkern_cflags := $(CFLAGS_MODULE)
$(real-objs-m:.o=.s): modkern_cflags := $(CFLAGS_MODULE)

$(export-objs)      : export_flags := $(EXPORT_FLAGS)

c_flags = $(CFLAGS) $(modkern_cflags) $(EXTRA_CFLAGS) $(CFLAGS_$(*F).o) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) $(export_flags)

cmd_cc_s_c = $(CC) $(c_flags) -S $< -o $@

%.s: %.c FORCE
	$(call if_changed,cmd_cc_s_c)

cmd_cc_i_c = $(CPP) $(c_flags) $< > $@

%.i: %.c FORCE
	$(call if_changed,cmd_cc_i_c)

cmd_cc_o_c = $(CC) $(c_flags) -c -o $@ $<

%.o: %.c FORCE
	$(call if_changed,cmd_cc_o_c)

# Compile assembler sources (.S)
# ---------------------------------------------------------------------------

# FIXME (s.a.)
modkern_aflags := $(AFLAGS_KERNEL)

$(real-objs-y)      : modkern_aflags := $(AFLAGS_KERNEL)
$(real-objs-y:.o=.s): modkern_aflags := $(AFLAGS_KERNEL)

$(real-objs-m)      : modkern_aflags := $(AFLAGS_MODULE)
$(real-objs-m:.o=.s): modkern_aflags := $(AFLAGS_MODULE)

a_flags = $(AFLAGS) $(modkern_aflags) $(EXTRA_AFLAGS) $(AFLAGS_$(*F).o)

cmd_as_s_S = $(CPP) $(a_flags) $< > $@

%.s: %.S FORCE
	$(call if_changed,cmd_as_s_S)

cmd_as_o_S = $(CC) $(a_flags) -c -o $@ $<

%.o: %.S FORCE
	$(call if_changed,cmd_as_o_S)

# FIXME

%.lst: %.c
	$(CC) $(c_flags) -g -c -o $*.o $<
	$(TOPDIR)/scripts/makelst $* $(TOPDIR) $(OBJDUMP)


# If a Makefile does define neither O_TARGET nor L_TARGET,
# use a standard O_TARGET named "built-in.o"

ifndef O_TARGET
ifndef L_TARGET
O_TARGET := built-in.o
endif
endif

# Build the compiled-in targets
# ---------------------------------------------------------------------------

all_targets: $(O_TARGET) $(L_TARGET) sub_dirs

# To build objects in subdirs, we need to descend into the directories
$(sort $(subdir-obj-y)): sub_dirs ;

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
# If the list of objects to link is empty, just create an empty O_TARGET
cmd_link_o_target = $(if $(strip $(obj-y)),\
		      $(LD) $(EXTRA_LDFLAGS) -r -o $@ $(filter $(obj-y), $^),\
		      rm -f $@; $(AR) rcs $@)

$(O_TARGET): $(obj-y) FORCE
	$(call if_changed,cmd_link_o_target)
endif # O_TARGET

#
# Rule to compile a set of .o files into one .a file
#
ifdef L_TARGET
cmd_link_l_target = rm -f $@; $(AR) $(EXTRA_ARFLAGS) rcs $@ $(obj-y)

$(L_TARGET): $(obj-y) FORCE
	$(call if_changed,cmd_link_l_target)
endif

#
# Rule to link composite objects
#


cmd_link_multi = $(LD) $(EXTRA_LDFLAGS) -r -o $@ $(filter $($(basename $@)-objs),$^)

# We would rather have a list of rules like
# 	foo.o: $(foo-objs)
# but that's not so easy, so we rather make all composite objects depend
# on the set of all their parts
$(multi-used-y) : %.o: $(multi-objs-y) FORCE
	$(call if_changed,cmd_link_multi)

$(multi-used-m) : %.o: $(multi-objs-m) FORCE
	$(call if_changed,cmd_link_multi)

#
# This make dependencies quickly
#
fastdep: FORCE
	$(TOPDIR)/scripts/mkdep $(CFLAGS) $(EXTRA_CFLAGS) -- $(wildcard *.[chS]) > .depend
ifdef ALL_SUB_DIRS
	$(MAKE) $(patsubst %,_sfdep_%,$(ALL_SUB_DIRS)) _FASTDEP_ALL_SUB_DIRS="$(ALL_SUB_DIRS)"
endif

ifdef _FASTDEP_ALL_SUB_DIRS
$(patsubst %,_sfdep_%,$(_FASTDEP_ALL_SUB_DIRS)):
	@$(MAKE) -C $(patsubst _sfdep_%,%,$@) fastdep
endif


#
# A rule to make subdirectories
#
subdir-list = $(sort $(patsubst %,_subdir_%,$(SUB_DIRS)))
sub_dirs: FORCE $(subdir-list)

ifdef SUB_DIRS
$(subdir-list) : FORCE
	@$(MAKE) -C $(patsubst _subdir_%,%,$@)
endif

#
# A rule to make modules
#
ifneq "$(strip $(MOD_SUB_DIRS))" ""
.PHONY: $(patsubst %,_modsubdir_%,$(MOD_SUB_DIRS))
$(patsubst %,_modsubdir_%,$(MOD_SUB_DIRS)) : FORCE
	@$(MAKE) -C $(patsubst _modsubdir_%,%,$@) modules

.PHONY: $(patsubst %,_modinst_%,$(MOD_SUB_DIRS))
$(patsubst %,_modinst_%,$(MOD_SUB_DIRS)) : FORCE
	@$(MAKE) -C $(patsubst _modinst_%,%,$@) modules_install
endif

.PHONY: modules
modules: $(obj-m) FORCE \
	 $(patsubst %,_modsubdir_%,$(MOD_SUB_DIRS))

.PHONY: _modinst__
_modinst__: FORCE
ifneq "$(strip $(obj-m))" ""
	mkdir -p $(MODLIB)/kernel/$(RELDIR)
	cp $(obj-m) $(MODLIB)/kernel/$(RELDIR)
endif

.PHONY: modules_install
modules_install: _modinst__ \
	 $(patsubst %,_modinst_%,$(MOD_SUB_DIRS))


# Add FORCE to the prequisites of a target to force it to be always rebuilt.
# ---------------------------------------------------------------------------
.PHONY: FORCE
FORCE:

#
# This is useful for testing
# FIXME: really?
script:
	$(SCRIPT)

#
# This sets version suffixes on exported symbols
# Separate the object into "normal" objects and "exporting" objects
# Exporting objects are: all objects that define symbol tables
#
ifdef CONFIG_MODULES

multi-objs	:= $(foreach m, $(obj-y) $(obj-m), $($(basename $(m))-objs))
active-objs	:= $(sort $(multi-objs) $(obj-y) $(obj-m))

ifdef CONFIG_MODVERSIONS
ifneq "$(strip $(export-objs))" ""

MODINCL := $(TOPDIR)/include/linux/modules
MODPREFIX := $(subst /,-,$(RELDIR))__

#
# Added the SMP separator to stop module accidents between uniprocessor
# and SMP Intel boxes - AC - from bits by Michael Chastain
#

ifdef CONFIG_SMP
	genksyms_smp_prefix := -p smp_
else
	genksyms_smp_prefix := 
endif

$(MODINCL)/$(MODPREFIX)%.ver: %.c
	@if [ ! -r $(MODINCL)/$(MODPREFIX)$*.stamp -o $(MODINCL)/$(MODPREFIX)$*.stamp -ot $< ]; then \
		echo '$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -E -D__GENKSYMS__ $<'; \
		echo '| $(GENKSYMS) $(genksyms_smp_prefix) -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp'; \
		$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -E -D__GENKSYMS__ $< \
		| $(GENKSYMS) $(genksyms_smp_prefix) -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp; \
		if [ -r $@ ] && cmp -s $@ $@.tmp; then echo $@ is unchanged; rm -f $@.tmp; \
		else echo mv $@.tmp $@; mv -f $@.tmp $@; fi; \
	fi; touch $(MODINCL)/$(MODPREFIX)$*.stamp

$(addprefix $(MODINCL)/$(MODPREFIX),$(export-objs:.o=.ver)): $(TOPDIR)/include/linux/autoconf.h

# updates .ver files but not modversions.h
fastdep: $(addprefix $(MODINCL)/$(MODPREFIX),$(export-objs:.o=.ver))

# updates .ver files and modversions.h like before (is this needed?)
dep: fastdep update-modverfile

endif # export-objs 

# update modversions.h, but only if it would change
update-modverfile:
	@(echo "#ifndef _LINUX_MODVERSIONS_H";\
	  echo "#define _LINUX_MODVERSIONS_H"; \
	  echo "#include <linux/modsetver.h>"; \
	  cd $(TOPDIR)/include/linux/modules; \
	  for f in *.ver; do \
	    if [ -f $$f ]; then echo "#include <linux/modules/$${f}>"; fi; \
	  done; \
	  echo "#endif"; \
	) > $(TOPDIR)/include/linux/modversions.h.tmp
	@if [ -r $(TOPDIR)/include/linux/modversions.h ] && cmp -s $(TOPDIR)/include/linux/modversions.h $(TOPDIR)/include/linux/modversions.h.tmp; then \
		echo $(TOPDIR)/include/linux/modversions.h was not updated; \
		rm -f $(TOPDIR)/include/linux/modversions.h.tmp; \
	else \
		echo $(TOPDIR)/include/linux/modversions.h was updated; \
		mv -f $(TOPDIR)/include/linux/modversions.h.tmp $(TOPDIR)/include/linux/modversions.h; \
	fi

$(active-objs): $(TOPDIR)/include/linux/modversions.h

else

$(TOPDIR)/include/linux/modversions.h:
	@echo "#include <linux/modsetver.h>" > $@

endif # CONFIG_MODVERSIONS

ifneq "$(strip $(export-objs))" ""

$(export-objs): $(TOPDIR)/include/linux/modversions.h

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

# ---------------------------------------------------------------------------
# Check if command line has changed

# Usage:
# normally one uses rules like
#
# %.o: %.c
# 	<command line>
#
# However, these only rebuild the target when the source has changed,
# but not when e.g. the command or the flags on the command line changed.
#
# This extension allows to do the following:
#
# command = <command line>
#
# %.o: %.c dummy
#	$(call if_changed,command)
#
# which will make sure to rebuild the target when either its prerequisites
# change or the command line changes
#
# The magic works as follows:
# The addition of dummy to the dependencies causes the rule for rebuilding
# to be always executed. However, the if_changed function will generate
# an empty command when 
# o none of the prequesites changed (i.e $? is empty)
# o the command line did not change (we compare the old command line,
#   which is saved in .<target>.o, to the current command line using
#   the two filter-out commands)

# read all saved command lines

cmd_files := $(wildcard .*.cmd)
ifneq ($(cmd_files),)
  include $(cmd_files)
endif

# function to only execute the passed command if necessary

if_changed = $(if $(strip $? \
		          $(filter-out $($(1)),$(cmd_$(@F)))\
			  $(filter-out $(cmd_$(@F)),$($(1)))),\
	       @echo '$($(1))' && $($(1)) && echo 'cmd_$@ := $($(1))' > $(@D)/.$(@F).cmd)

