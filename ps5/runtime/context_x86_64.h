#pragma once

/* Same SysV entry ABI on PS5 and in the Windows host verification harness.
 * Stack allocation and the single-thread scheduler policy belong to HostContext.
 * The stack must remain alive while its continuation can be resumed. */
#if defined(_WIN32) && defined(__clang__)
#define MKW_CONTEXT_ABI __attribute__((sysv_abi))
#else
#define MKW_CONTEXT_ABI
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef void (MKW_CONTEXT_ABI *MkwContextEntry)(void*);
void MKW_CONTEXT_ABI mkw_ps5_context_switch(void** target_sp, void** source_sp);
/* stack_top points just beyond an allocated stack of at least 16 KiB.
 * Inherits the creator's floating-point controls. entry must never return. */
void* MKW_CONTEXT_ABI mkw_ps5_context_init(void* stack_top,
                                         MkwContextEntry entry, void* argument);
#ifdef __cplusplus
}
#endif
