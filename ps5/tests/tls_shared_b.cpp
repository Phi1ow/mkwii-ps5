inline thread_local int sharedZero = 0;
inline thread_local int sharedInitialized = 17;
extern "C" int* zeroB() { return &sharedZero; }
extern "C" int* initializedB() { return &sharedInitialized; }
