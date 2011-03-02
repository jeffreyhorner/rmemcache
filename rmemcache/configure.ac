AC_INIT(RMEMCACHE, 0.1, Jeff.Horner@vanderbilt.edu)
AC_CONFIG_SRCDIR([src/rmemcache.c])
AC_CONFIG_HEADER([src/config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([errno.h fcntl.h limits.h netdb.h netinet/in.h stdlib.h string.h strings.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_REALLOC
AC_CHECK_FUNCS([gethostbyname socket strchr strdup])
AC_OUTPUT
