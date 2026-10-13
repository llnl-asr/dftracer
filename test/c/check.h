#ifndef DFTRACER_TEST_C_CHECK_H
#define DFTRACER_TEST_C_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* The C twin of test/unit/check.h: unlike assert(), it still checks under
   NDEBUG. */
#define DFT_CHECK(cond)                                               \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "CHECK FAILED: %s at %s:%d\n", #cond, __FILE__, \
              __LINE__);                                              \
      exit(1);                                                        \
    }                                                                 \
  } while (0)

#endif /* DFTRACER_TEST_C_CHECK_H */
