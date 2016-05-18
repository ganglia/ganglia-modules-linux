AM_CFLAGS  = -I$(top_builddir)/include -I$(top_builddir)/libmetrics -I$(top_builddir)/lib

if STATIC_BUILD

noinst_LTLIBRARIES    = libmoduser.la
libmoduser_la_SOURCES = mod_user.c
libmoduser_la_LDFLAGS = -export-all-symbols

else

pkglib_LTLIBRARIES    = moduser.la
moduser_la_SOURCES = mod_user.c
moduser_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = ../conf.d/mod_user.conf

endif

AM_CPPFLAGS = @APR_INCLUDES@

pkglibdir = $(libdir)/ganglia

