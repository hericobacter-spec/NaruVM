.section .rodata
.p2align 2
.global lab_arith_begin, lab_arith_end
lab_arith_begin:
    mov x0, #40
    add x0, x0, #2
    mov x1, #10
    mov x2, #0
1:  add x2, x2, x1
    subs x1, x1, #1
    b.ne 1b
    mov x6, #11
    mov x7, #13
    add x15, x6, x7
    mov x18, #77
    add x28, x18, #3
    b lab_arith_end
lab_arith_end:
    nop

.global lab_memory_begin, lab_memory_end
lab_memory_begin:
    movz x1, #0x8001, lsl #16
    mov x0, #1234
    str x0, [x1]
    ldr x2, [x1]
    ldaxr x3, [x1]
    add x3, x3, #1
    stlxr w4, x3, [x1]
    ldr x5, [x1]
    b lab_memory_end
lab_memory_end:
    nop

.global lab_simd_begin, lab_simd_end
lab_simd_begin:
    fmov d0, #1.5
    fmov d1, #2.0
    fadd d2, d0, d1
    movi v3.4s, #7
    add v4.4s, v3.4s, v3.4s
    b lab_simd_end
lab_simd_end:
    nop

.global lab_timer_begin, lab_timer_end
lab_timer_begin:
    mrs x0, cntfrq_el0
    mrs x1, cntvct_el0
    mrs x2, cntvct_el0
    b lab_timer_end
lab_timer_end:
    nop
.global lab_smp_primary, lab_smp_primary_end, lab_smp_secondary, lab_smp_secondary_end
lab_smp_primary:
    movz x20, #0x8000, lsl #16
    add x20, x20, #8, lsl #12
    movz x0, #3
    movk x0, #0xc400, lsl #16
    mov x1, #1
    movz x2, #0x8000, lsl #16
    add x2, x2, #4, lsl #12
    mov x3, #0x42
    hvc #0
    str x0, [x20]
    add x21, x20, #16
1:  ldar x4, [x21]
    cbz x4, 1b
    mov x18, #77
    movz x0, #8
    movk x0, #0x8400, lsl #16
    hvc #0
    b .
lab_smp_primary_end:
lab_smp_secondary:
    movz x20, #0x8000, lsl #16
    add x20, x20, #8, lsl #12
    str x0, [x20, #8]
    add x21, x20, #16
    mov x4, #1
    stlr x4, [x21]
    mov x18, #99
    movz x0, #2
    movk x0, #0x8400, lsl #16
    hvc #0
    b .
lab_smp_secondary_end:
.section .note.GNU-stack,"",%progbits

/* Authored performance loop: data occupies a different guest page from code. */
.section .rodata
.balign 4
.global lab_perf_begin
.global lab_perf_end
lab_perf_begin:
    movz x0, #0xffff
    movk x0, #0x1f, lsl #16
    movz x1, #0
    movk x1, #0x8001, lsl #16
.Llab_perf_loop:
    ldr x2, [x1]
    add x2, x2, #1
    str x2, [x1]
    subs x0, x0, #1
    b.ne .Llab_perf_loop
lab_perf_end:
    hlt #0

/* Four owned vCPUs: PSCI start, release/acquire acknowledgement, CPU_OFF,
 * then SYSTEM_OFF. Context and result slots are distinct for every CPU. */
.section .rodata
.balign 4
.global lab_smp4_primary, lab_smp4_primary_end
.macro smp4_on index
    movz x0, #3
    movk x0, #0xc400, lsl #16
    mov x1, #\index
    movz x2, #0x8000, lsl #16
    add x2, x2, #4, lsl #12
    mov x3, #\index
    hvc #0
    str x0, [x20, #(\index * 8)]
.endm
.macro smp4_wait offset, value
    add x21, x20, #\offset
.Lsmp4_wait_\@:
    ldar x4, [x21]
    cmp x4, #\value
    b.ne .Lsmp4_wait_\@
.endm
lab_smp4_primary:
    movz x20, #0x8000, lsl #16
    add x20, x20, #8, lsl #12
    smp4_on 1
    smp4_on 2
    smp4_on 3
    smp4_wait 72, 1
    smp4_wait 80, 2
    smp4_wait 88, 3
    mov x4, #1
    stlr x4, [x20]
    smp4_wait 136, 65
    smp4_wait 144, 66
    smp4_wait 152, 67
    movz x0, #8
    movk x0, #0x8400, lsl #16
    hvc #0
    b .
lab_smp4_primary_end:
.global lab_smp4_secondary, lab_smp4_secondary_end
lab_smp4_secondary:
    movz x20, #0x8000, lsl #16
    add x20, x20, #8, lsl #12
    add x21, x20, x0, lsl #3
    add x22, x21, #64
    stlr x0, [x22]
1:  ldar x4, [x20]
    cbz x4, 1b
    add x22, x21, #128
    add x4, x0, #64
    stlr x4, [x22]
    movz x0, #2
    movk x0, #0x8400, lsl #16
    hvc #0
    b .
lab_smp4_secondary_end:
