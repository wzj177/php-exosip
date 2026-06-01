PHP_ARG_ENABLE([exosip], 
  [whether to enable exosip support],
  [AS_HELP_STRING([--enable-exosip], [Enable exosip support])],
  [no])

if test "$PHP_EXOSIP" != "no"; then
  AC_DEFINE([HAVE_EXOSIP], [1], [Whether you have exosip support])
  
  dnl Set paths
  if test "$PHP_EXOSIP" = "yes"; then
    EXOSIP_DIR="."
  else
    EXOSIP_DIR="$PHP_EXOSIP"
  fi
  
  dnl Determine library directory structure (support both layouts)
  if test -d "$EXOSIP_DIR/lib"; then
    EXOSIP_LIB_DIR="$EXOSIP_DIR/lib"
  elif test -d "$EXOSIP_DIR/libs/lib"; then
    EXOSIP_LIB_DIR="$EXOSIP_DIR/libs/lib"
  else
    AC_MSG_ERROR([Cannot find lib directory in $EXOSIP_DIR])
  fi
  
  dnl Check for required static libraries
  AC_MSG_CHECKING([for eXosip2 static library])
  if test -f "$EXOSIP_LIB_DIR/libeXosip2.a"; then
    AC_MSG_RESULT([found in $EXOSIP_LIB_DIR])
  else
    AC_MSG_ERROR([libeXosip2.a not found in $EXOSIP_LIB_DIR])
  fi
  
  AC_MSG_CHECKING([for osip2 static library])
  if test -f "$EXOSIP_LIB_DIR/libosip2.a"; then
    AC_MSG_RESULT([found])
  else
    AC_MSG_ERROR([libosip2.a not found in $EXOSIP_LIB_DIR])
  fi
  
  AC_MSG_CHECKING([for osipparser2 static library])
  if test -f "$EXOSIP_LIB_DIR/libosipparser2.a"; then
    AC_MSG_RESULT([found])
  else
    AC_MSG_ERROR([libosipparser2.a not found in $EXOSIP_LIB_DIR])
  fi
  
  dnl Determine include directory structure (support both layouts)
  if test -d "$EXOSIP_DIR/include"; then
    EXOSIP_INC_DIR="$EXOSIP_DIR/include"
  elif test -d "$EXOSIP_DIR/libs/include"; then
    EXOSIP_INC_DIR="$EXOSIP_DIR/libs/include"
  else
    AC_MSG_ERROR([Cannot find include directory in $EXOSIP_DIR])
  fi

  dnl Add include paths
  PHP_ADD_INCLUDE([$EXOSIP_INC_DIR])
  PHP_ADD_INCLUDE([$EXOSIP_INC_DIR/eXosip2])
  PHP_ADD_INCLUDE([$EXOSIP_INC_DIR/osip2])
  PHP_ADD_INCLUDE([$EXOSIP_INC_DIR/osipparser2])
  
  dnl Add static libraries (Linux)
  dnl GNU ld is single-pass; order must be: dependent first, dependency after.
  dnl --start-group/--end-group handles circular deps between archives.
  EXOSIP_SHARED_LIBADD="-Wl,--start-group $EXOSIP_LIB_DIR/libeXosip2.a $EXOSIP_LIB_DIR/libosip2.a $EXOSIP_LIB_DIR/libosipparser2.a -Wl,--end-group $EXOSIP_SHARED_LIBADD"
  PHP_EVAL_LIBLINE([-lresolv -lpthread -lrt -ldl], [EXOSIP_SHARED_LIBADD])

  dnl CentOS/GCC may default to gnu89, but the extension uses C99 syntax
  dnl such as for-loop initial declarations: for (int i = ...).
  CFLAGS="$CFLAGS -std=gnu99"
  
  dnl Set source files
  PHP_NEW_EXTENSION([exosip], [php_exosip.c exosip_wrapper.c], [$ext_shared])
  PHP_SUBST([EXOSIP_SHARED_LIBADD])
fi
