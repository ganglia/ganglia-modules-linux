AM_CFLAGS  = -D_LARGEFILE64_SOURCE -I$(top_builddir)/include -I$(top_builddir)/libmetrics -I$(top_builddir)/lib 

if STATIC_BUILD
noinst_LTLIBRARIES    = libmodfs.la
libmodfs_la_SOURCES = mod_fs.c
libmodfs_la_LDFLAGS = -export-all-symbols
else
pkglib_LTLIBRARIES    = modfs.la

modfs_la_SOURCES = mod_fs.c 
modfs_la_LDFLAGS = -module -avoid-version
#modfs_la_LIBADD = $(top_builddir)/libmetrics/libmetrics.la

EXTRA_DIST = ../conf.d/mod_fs.conf
endif

#INCLUDES = @APR_INCLUDES@

pkglibdir = $(libdir)/ganglia

