#ifndef PHP_EXOSIP_H
#define PHP_EXOSIP_H

extern zend_module_entry exosip_module_entry;
#define phpext_exosip_ptr &exosip_module_entry

#define PHP_EXOSIP_VERSION "1.0.1-tcp-worker"

#ifdef PHP_WIN32
#   define PHP_EXOSIP_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#   define PHP_EXOSIP_API __attribute__ ((visibility("default")))
#else
#   define PHP_EXOSIP_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/* 
  Declare any global variables you may need between the BEGIN
  and END macros here:

ZEND_BEGIN_MODULE_GLOBALS(exosip)
ZEND_END_MODULE_GLOBALS(exosip)
*/

/* Always refer to the globals in your function as EXOSIP_G(variable).
   You are encouraged to rename these macros something shorter, see
   examples in any other php module directory.
*/
#define EXOSIP_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(exosip, v)

#if defined(ZTS) && defined(COMPILE_DL_EXOSIP)
ZEND_TSRMLS_CACHE_EXTERN
#endif

#endif	/* PHP_EXOSIP_H */
