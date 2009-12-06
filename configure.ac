AC_INIT(mod_sqltemplate, 1.0)
MAKE_CONFIG_NICE(config.nice)
AC_PREREQ(2.53)

AC_CONFIG_SRCDIR([src/mod_sqltemplate.c])
AC_CONFIG_AUX_DIR(config)
AC_PROG_LIBTOOL
AM_MAINTAINER_MODE
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

AM_CONFIG_HEADER([src/mod_sqltemplate_config.h:config.in])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_LD
AC_PROG_INSTALL

AP_VERSION=2.2.9
CHECK_APACHE(,$AP_VERSION,
    :,:,
    AC_MSG_ERROR([*** Apache version $AP_VERSION not found!])
)

AX_LIB_MYSQL("4.1")

prefix=${AP_PREFIX}

LIBTOOL="`${APR_CONFIG} --apr-libtool`"
AC_SUBST(LIBTOOL)

MODULE_CFLAGS="${APXS_CFLAGS} ${AP_INCLUDES} ${APR_INCLUDES} ${APU_INCLUDES} ${MYSQL_CFLAGS}"
AC_SUBST(MODULE_CFLAGS)

MODULE_LDFLAGS=" `${APR_CONFIG} --link-libtool` `${APU_CONFIG} --link-libtool` ${MYSQL_LDFLAGS}"
AC_SUBST(MODULE_LDFLAGS)

BIN_LDFLAGS=" `${APR_CONFIG} --link-libtool` `${APU_CONFIG} --link-libtool` `${APU_CONFIG} --ldflags --libs` `${APR_CONFIG} --ldflags --libs` ${MYSQL_LDFLAGS}"
AC_SUBST(BIN_LDFLAGS)

dnl this should be a list to all of the makefiles you expect to be generated
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
