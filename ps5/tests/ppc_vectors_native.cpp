#include "isa/ppc_isa_float.h"
extern "C" void mkw_diagnostic_log(const char*);
// Volatile operands prevent constant folding: the console must execute the
// same inline FMA primitive as the translated game. The first lane is a
// cancellation case that distinguishes fused from separately rounded math.
extern "C" int mkw_ppc_vectors_test() {
    volatile float a = 0x1.000002p0f, b = 0x1.fffffcp-1f;
    float result[4];
    _mm_storeu_ps(result, PpcFmaddPairInline(_mm_set_ps(4, 3, 2, a), _mm_set_ps(2, 2, 2, b), _mm_set_ps(1, 1, 1, -1)));
    if (result[0] != -0x1p-46f || result[1] != 5 || result[2] != 7 || result[3] != 9) return 6;
    _mm_storeu_ps(result, PpcFmsubPairInline(_mm_set1_ps(3), _mm_set1_ps(2), _mm_set1_ps(1)));
    for (float lane : result) if (lane != 5) return 7;
    mkw_diagnostic_log("[mkw-cpu] PASS real WiiCompiled FMA/FMSUB primitives including fused cancellation\n");
    return 0;
}
