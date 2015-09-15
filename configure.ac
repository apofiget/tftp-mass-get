#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([TFTP mass get tool], [0.3.1], [https://github.com/apofiget/tftp-mass-get/issues], [tftp_mass_get], [https://github.com/apofiget/tftp-mass-get])
AC_CONFIG_SRCDIR([src/tftp-get.c])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB(pthread, pthread_create, [ LIBS="-lpthread -lrt $LIBS" ], [AC_MSG_ERROR([libpthread not found])])
AC_CHECK_LIB(curl, curl_global_init, [ LIBS="-lcurl $LIBS" ], [AC_MSG_ERROR([libcurl-dev not found])])
AC_CHECK_LIB(config, config_init, [ LIBS="-lconfig $LIBS" ], [AC_MSG_ERROR([libconfig-dev not found])])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h time.h inttypes.h stdlib.h string.h syslog.h libconfig.h unistd.h curl/curl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memmove strchr strerror strncasecmp])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
