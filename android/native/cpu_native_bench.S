.text
.p2align 4
.global lab_cpu_memory_begin, lab_cpu_memory_end
.type lab_cpu_memory_begin, %function
lab_cpu_memory_begin:
    bti c
1:  ldr x2, [x0]
    add x2, x2, #1
    str x2, [x0]
    subs x1, x1, #1
    b.ne 1b
lab_cpu_memory_end:
    ret
.size lab_cpu_memory_begin, .-lab_cpu_memory_begin

.p2align 4
.global lab_cpu_alu_begin, lab_cpu_alu_end
.type lab_cpu_alu_begin, %function
lab_cpu_alu_begin:
    bti c
    mov x2, #1
    mov x3, #17
1:  add x2, x2, x3
    eor x3, x3, x2
    ror x2, x2, #7
    subs x1, x1, #1
    b.ne 1b
    str x2, [x0]
lab_cpu_alu_end:
    ret
.size lab_cpu_alu_begin, .-lab_cpu_alu_begin

.p2align 4
.global lab_cpu_chase_begin, lab_cpu_chase_end
.type lab_cpu_chase_begin, %function
lab_cpu_chase_begin:
    bti c
    mov x2, #0
1:  ldr x2, [x0, x2]
    subs x1, x1, #1
    b.ne 1b
    str x2, [x0]
lab_cpu_chase_end:
    ret
.size lab_cpu_chase_begin, .-lab_cpu_chase_begin
.section .note.GNU-stack,"",%progbits
