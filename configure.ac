dnl    ganglia-modules-linux: modules for collecting metrics on Linux
dnl    Copyright (C) 2011 Daniel Pocock
dnl
dnl    This program is free software: you can redistribute it and/or modify
dnl    it under the terms of the GNU General Public License as published by
dnl    the Free Software Foundation, either version 3 of the License, or
dnl    (at your option) any later version.
dnl
dnl    This program is distributed in the hope that it will be useful,
dnl    but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl    GNU General Public License for more details.
dnl
dnl    You should have received a copy of the GNU General Public License
dnl    along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_INIT(ganglia-modules-linux,1.3.1)
AC_CONFIG_SRCDIR(example/mod_example.c)
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])

AC_CANONICAL_HOST
AM_CONFIG_HEADER(config.h)

dnl The following cpu_vendor_os string goes into config.h
dnl
AC_DEFINE_UNQUOTED(HOST_OS, "$HOST_OS", HOST_OS)
AC_DEFINE_UNQUOTED(CPU_VENDOR_OS, "$HOST", CPU_VENDOR_OS)
dnl AC_CYGWIN

AC_ARG_ENABLE( static-build,
[ --enable-static-build  Generate objects for static linking to gmond  ],
[ enable_static_build=yes ],[ enable_static_build=no ])

AM_CONDITIONAL(STATIC_BUILD, test x"$enable_static_build" = xyes)

AC_PROG_CC

AC_PROG_INSTALL

AC_PROG_LIBTOOL

dnl default VARSTATEDIR to /var/lib since that's the traditional location.
dnl
varstatedir="/var/lib"

OS="unknown"
EXPORT_SYMBOLS="-export-dynamic"
case "$host" in
*linux*)
		CFLAGS="$CFLAGS -D_REENTRANT"
		AC_DEFINE(LINUX, 1, LINUX)
		OS="linux"
dnl
dnl For fsuage.c - disk usage.
dnl
		AC_DEFINE(STAT_STATVFS, 1, STAT_STATVFS)
		AC_DEFINE(SUPPORT_GEXEC, 1, SUPPORT_GEXEC)
		;;
*solaris*)	AC_DEFINE(SUPPORT_GEXEC, 0, SUPPORT_GEXEC)
		CFLAGS="$CFLAGS -D__EXTENSIONS__ -DHAVE_STRERROR"
		if test x"$ac_cv_prog_cc_c99" = x -o x"$ac_cv_prog_cc_c99" = xno; then
			CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=199506L"
		else 
			CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=200112L"
		fi
		if test x"$ac_cv_prog_gcc" != xyes; then
			LIBS="-lm $LIBS"
		fi
		AC_DEFINE(SOLARIS,1,SOLARIS)
		OS="solaris"
		;;
*cygwin*)	LDFLAGS="-L/bin"
		EXPORT_SYMBOLS="-export-all-symbols"
		AC_DEFINE(CYGWIN, 1, CYGWIN)
		;; 
esac

AC_SUBST(OS)

AC_SUBST(EXPORT_SYMBOLS)
dnl Define VARSTATEDIR in config.h
dnl
AC_SUBST(varstatedir)
AC_DEFINE_UNQUOTED(VARSTATEDIR, "$varstatedir", VARSTATEDIR)

if test x"$libdir" = x"\${exec_prefix/lib"; then
  if test x"$exec_prefix" = xNONE; then
    if test x"$prefix" = xNONE; then
      exec_prefix="$ac_default_prefix"
    else
      exec_prefix="$prefix"
    fi
  fi
  cfg_libdir=`eval echo "$libdir"`
fi
AC_SUBST(cfg_libdir)

ganglia_sysconfdir="${sysconfdir}/ganglia"
AC_SUBST(ganglia_sysconfdir)

#AC_OUTPUT(Makefile ganglia-gmond-modules.spec etc/Makefile etc/conf.d/module1.conf io/Makefile)

AC_OUTPUT(Makefile example/Makefile fs/Makefile multicpu/Makefile io/Makefile)


