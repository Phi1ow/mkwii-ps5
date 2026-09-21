/* Freestanding C++ bring-up: no standard library or exception runtime yet.
 * Keep this separate from the validated cube until its own console trial. */
static_assert(sizeof(void*) == 8 && sizeof(long) == 8, "PS5 LP64 ABI required");
static volatile unsigned constructor_value;
struct StartupState {
    StartupState() { constructor_value = 0x42; }
};
static StartupState startup_state;
static thread_local volatile unsigned tls_value = 7;

extern "C" int mkw_cpp_runtime_smoke() {
    if (constructor_value != 0x42 || tls_value != 7) return 1;
    tls_value = 19;
    return tls_value == 19 ? 0 : 2;
}
#ifndef MKW_CPP_SMOKE_LIBRARY
extern "C" int main() { return mkw_cpp_runtime_smoke(); }
#endif
