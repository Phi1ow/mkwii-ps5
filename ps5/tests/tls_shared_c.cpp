extern thread_local int sharedZero;
extern thread_local int sharedInitialized;
extern "C" int* zeroC() { return &sharedZero; }
extern "C" int* initializedC() { return &sharedInitialized; }
