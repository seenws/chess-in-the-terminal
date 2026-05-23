#ifndef CITT_HEADERS_DEBUG_H_
#define CITT_HEADERS_DEBUG_H_

#include <stdio.h>
#include <stdlib.h>

#ifdef DEBUG
  #define DBG_PRINTF(...) do { fprintf(stderr, __VA_ARGS__); } while (0)
  #define DBG_ASSERT(x) do {                                                       \
      if (!(x)) {                                                                  \
          fprintf(stderr, "DBG_ASSERT failed: %s at %s:%d\n",                      \
                  #x, __FILE__, __LINE__);                                         \
          abort();                                                                 \
      }                                                                            \
  } while (0)
#else
  #define DBG_PRINTF(...) ((void)0)
  #define DBG_ASSERT(x)   ((void)0)
#endif /* DEBUG */

#endif /* CITT_HEADERS_DEBUG_H_ */
