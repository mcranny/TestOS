#ifndef CONFIG_H
#define CONFIG_H

/* Define TESTOS_SELFTEST (e.g. make CFLAGS_EXTRA=-DTESTOS_SELFTEST) to run
 * filesystem stress tests during boot after fs_initialize. */

#ifndef TESTOS_DEBUG
#define TESTOS_DEBUG 1
#endif

#endif
