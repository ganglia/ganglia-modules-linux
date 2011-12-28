AM_CFLAGS  = -D_LARGEFILE64_SOURCE -I$(top_builddir)/include -I$(top_builddir)/libmetrics -I$(top_builddir)/lib 

if STATIC_BUILD
noinst_LTLIBRARIES    = libmodio.la
libmodio_la_SOURCES = mod_io.c
libmodio_la_LDFLAGS = -export-all-symbols
else
pkglib_LTLIBRARIES    = modio.la

modio_la_SOURCES = mod_io.c 
modio_la_LDFLAGS = -module -avoid-version
#modio_la_LIBADD = $(top_builddir)/libmetrics/libmetrics.la

EXTRA_DIST = ../conf.d/mod_io.conf
endif

INCLUDES = @APR_INCLUDES@

pkglibdir = $(libdir)/ganglia

