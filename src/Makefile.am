# Build-deps:
#   libnl-route-3-dev

bin_PROGRAMS = \
  tcmmd \
  tcdemo \
  $(NULL)

BUILT_SOURCES = \
  tcmmd-generated.h \
  tcmmd-generated.c \
  $(NULL)

tcmmd_SOURCES = \
  tcmmd.c \
  tcmmd_rtnl.c \
  tcmmd_rtnl.h \
  tcmmd-dbus.c \
  tcmmd-dbus.h \
  tcmmd-generated.c \
  tcmmd-generated.h \
  $(NULL)

tcdemo_SOURCES = \
  tcdemo.c \
  tcmmd-generated.c \
  tcmmd-generated.h \
  $(NULL)

EXTRA_DIST = \
  gdbus-tcmmd.xml
  $(NULL)

#AM_CFLAGS = \
#  -g -Wall \
#  -I$(top_srcdir) -I$(top_builddir) \
#  $(NULL)

tcmmd_CFLAGS = @TCMMD_CFLAGS@
tcdemo_CFLAGS = @TCDEMO_CFLAGS@
  
tcmmd_LDADD = \
  @TCMMD_LIBS@ \
  $(NULL)

tcdemo_LDADD = \
  @TCDEMO_LIBS@ \
  $(NULL)

# do nothing, output as a side-effect
tcmmd-generated.c: tcmmd-generated-stamp
	@:
tcmmd-generated.h: tcmmd-generated-stamp
	@:

tcmmd-generated-stamp: gdbus-tcmmd.xml
	gdbus-codegen --interface-prefix org.tcmmd. \
              --generate-c-code tcmmd-generated \
              --c-namespace Tcmmd \
              $(srcdir)/gdbus-tcmmd.xml
	touch $@

CLEANFILES = \
  tcmmd-generated-stamp \
  $(BUILT_SOURCES)
  $(NULL)
