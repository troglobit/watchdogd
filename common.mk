# Some junk files we always want to be removed when doing a make clean.
JUNK       = *~ *.bak *.map .*.d DEADJOE semantic.cache *.gdb *.elf core core.*
MAKE      := @$(MAKE)
MAKEFLAGS  = --no-print-directory --silent
ARFLAGS   := crus
INSTALL   := install --backup=off
STRIPINST := $(INSTALL) -s --strip-program=$(CROSS)strip -m 0755

# Smart autodependecy generation via GCC -M.
.%.d: %.c
	@$(SHELL) -ec "$(CC) -MM $(CFLAGS) $(CPPFLAGS) $< \
		| sed 's,.*: ,$*.o $@ : ,g' > $@; \
                [ -s $@ ] || rm -f $@"

# Override default implicit rules
%.o: %.c
	@printf "  CC      $(subst $(ROOTDIR)/,,$(shell pwd)/$@)\n"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c -MMD -MP -o $@ $<

%: %.o
	@printf "  CC      $(subst $(ROOTDIR)/,,$(shell pwd)/$@)\n"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

