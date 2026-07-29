; COR24 stack probe A: current 3 KB EBR
; Region FEE000-FEEBFF, initial SP FEEC00.
; Pushes and verifies 1000 24-bit cells (3000 bytes).

start:
        la      r0,0FEEC00h
        mov     sp,r0
        la      r0,0123400h
        la      r2,1000

push_loop:
        push    r0
        add     r0,1
        add     r2,-1
        ceq     r2,z
        brf     push_loop

        la      r2,1000
pop_loop:
        add     r0,-1
        pop     r1
        ceq     r0,r1
        brf     fail
        add     r2,-1
        ceq     r2,z
        brf     pop_loop

        la      r1,0FEEC00h
        mov     r0,sp
        ceq     r0,r1
        brf     fail
        la      r0,pass_text
        la      r2,puts
        jal     r1,(r2)
halt:
        bra     halt

fail:
        la      r0,0FEEC00h
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
        .byte   83,84,65,67,75,32,69,66,82,51,32,80,65,83,83,13,10,0
fail_text:
        .byte   83,84,65,67,75,32,69,66,82,51,32,70,65,73,76,13,10,0
