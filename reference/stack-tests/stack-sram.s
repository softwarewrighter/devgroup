; COR24 stack probe B: stack in the top of 1 MB SRAM.
; Test window 0F0000-0FFFFF, initial SP 100000.
; Pushes and verifies 2048 24-bit cells (6144 bytes), proving >3 KB.

start:
        la      r0,0100000h
        mov     sp,r0
        la      r0,0234500h
        la      r2,2048

push_loop:
        push    r0
        add     r0,1
        add     r2,-1
        ceq     r2,z
        brf     push_loop

        la      r2,2048
pop_loop:
        add     r0,-1
        pop     r1
        ceq     r0,r1
        brf     fail
        add     r2,-1
        ceq     r2,z
        brf     pop_loop

        la      r1,0100000h
        mov     r0,sp
        ceq     r0,r1
        brf     fail
        la      r0,pass_text
        la      r2,puts
        jal     r1,(r2)
halt:
        bra     halt

fail:
        la      r0,0100000h
        mov     sp,r0
        la      r0,fail_text
        la      r2,puts
        jal     r1,(r2)
        bra     halt

; puts: r0 points at a zero-terminated string.
puts:
        push    r1
        push    r2
        mov     r2,r0
puts_next:
        lbu     r0,0(r2)
        ceq     r0,z
        brt     puts_done
        push    r0
puts_wait:
        la      r1,-65280
        lb      r0,1(r1)
        cls     r0,z
        brt     puts_wait
        pop     r0
        sb      r0,0(r1)
        add     r2,1
        bra     puts_next
puts_done:
        pop     r2
        pop     r1
        jmp     (r1)

pass_text:
        .byte   83,84,65,67,75,32,83,82,65,77,32,80,65,83,83,13,10,0
fail_text:
        .byte   83,84,65,67,75,32,83,82,65,77,32,70,65,73,76,13,10,0
