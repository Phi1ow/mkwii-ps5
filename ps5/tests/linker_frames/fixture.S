.section .text.shared,"axG",@progbits,shared,comdat
.weak shared
.type shared,@function
shared:
.cfi_startproc
.cfi_lsda 0x1b, .Llsda
#ifdef SECOND
subq $32, %rsp
.cfi_def_cfa_offset 40
addq $32, %rsp
.cfi_def_cfa_offset 8
#endif
ret
.cfi_endproc
.size shared, .-shared
.section .gcc_except_table.shared,"aG",@progbits,shared,comdat
.Llsda:
.byte 0xff,0xff,1,0
#ifdef SECOND
.text
.globl kept
.type kept,@function
kept:
.cfi_startproc
ret
.cfi_endproc
.size kept, .-kept
#endif
