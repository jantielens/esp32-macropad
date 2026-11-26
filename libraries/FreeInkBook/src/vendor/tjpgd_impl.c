/* Compiles the vendored TJpgDec (configured for grayscale output in
 * third_party/tjpgd/tjpgdcnf.h). TJpgDec works entirely inside a
 * caller-provided workspace — no allocation. */
#define jd_prepare freeink_jd_prepare
#define jd_decomp freeink_jd_decomp
#include "../../third_party/tjpgd/tjpgd.c"
