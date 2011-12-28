AM_CFLAGS  = -D_LARGEFILE64_SOURCE -I$(top_builddir)/include -I$(top_builddir)/libmetrics -I$(top_builddir)/lib 

if STATIC_BUILD
noinst_LTLIBRARIES    = libmodmulticpu.la
libmodmulticpu_la_SOURCES = mod_multicpu.c
libmodmulticpu_la_LDFLAGS = -export-all-symbols
else
pkglib_LTLIBRARIES    = modmulticpu.la

modmulticpu_la_SOURCES = mod_multicpu.c 
modmulticpu_la_LDFLAGS = -module -avoid-version
#modmulticpu_la_LIBADD = $(top_builddir)/libmetrics/libmetrics.la

EXTRA_DIST = ../conf.d/mod_multicpu.conf
endif

INCLUDES = @APR_INCLUDES@

pkglibdir = $(libdir)/ganglia

