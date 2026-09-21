inline thread_local int sharedZero = 0;
inline thread_local int sharedInitialized = 17;
extern "C" int* zeroA() { return &sharedZero; }
extern "C" int* initializedA() { return &sharedInitialized; }
extern "C" int _start() { return 0; }
