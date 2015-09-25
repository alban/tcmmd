AC_PREREQ([2.69])
AC_INIT(tcmmd, 0.1.12, alban.crequy@collabora.co.uk)
AM_INIT_AUTOMAKE([1.9 -Wno-portability foreign no-dist-gzip dist-xz tar-ustar])

AC_CONFIG_SRCDIR([src/tcmmd_rtnl.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for library functions.
LIBNL_REQ_VERSION=3.2.24

PKG_CHECK_MODULES(TCMMD,
                  [glib-2.0
                   gobject-2.0
                   gio-2.0
                   gio-unix-2.0
                   libnl-3.0 >= $LIBNL_REQ_VERSION
                   libnl-route-3.0 >= $LIBNL_REQ_VERSION])

PKG_CHECK_MODULES(TCDEMO,
                  [gstreamer-1.0
                   libsoup-2.4
                   gio-unix-2.0
                   clutter-gst-3.0])

AC_OUTPUT([
    Makefile
    data/Makefile
    src/Makefile
    tests/Makefile
  ])
