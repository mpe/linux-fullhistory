#
# This file contains rules which are shared between multiple Makefiles.
#

#
# Common rules
#
.c.s:
	$(CC) $(CFLAGS) -S $< -o $@

#
# A rule to do nothing
#
dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
