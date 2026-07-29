        .text

        .globl  _start
_start:
        la      r0,_main
        jal     r1,(r0)
_halt:
        bra     _halt

        .globl  _uart_putc
_uart_putc:
        push    fp
        push    r2
        push    r1
        mov     fp,sp
L1:
        la      r0,16711937
        lbu     r0,0(r0)
        la      r1,128
        and     r0,r1
        ceq     r0,z
        brt     L2
        bra     L1
L2:
        la      r0,16711936
        mov     r1,r0
        lw      r0,9(fp)
        sb      r0,0(r1)
L0:
        mov     sp,fp
        pop     r1
        pop     r2
        pop     fp
        jmp     (r1)

        .globl  _print_digits
_print_digits:
        push    fp
        push    r2
        push    r1
        mov     fp,sp
        add     sp,-3
        lc      r0,0
        sw      r0,-3(fp)
L4:
        lw      r0,-3(fp)
        lw      r1,12(fp)
        cls     r0,r1
        brf     L6
        lw      r0,9(fp)
        lw      r1,-3(fp)
        add     r0,r1
        lbu     r0,0(r0)
        push    r0
        la      r0,_uart_putc
        jal     r1,(r0)
        add     sp,3
L5:
        lw      r0,-3(fp)
        lc      r1,1
        add     r0,r1
        sw      r0,-3(fp)
        bra     L4
L6:
        lc      r0,13
        push    r0
        la      r0,_uart_putc
        jal     r1,(r0)
        add     sp,3
        lc      r0,10
        push    r0
        la      r0,_uart_putc
        jal     r1,(r0)
        add     sp,3
L3:
        mov     sp,fp
        pop     r1
        pop     r2
        pop     fp
        jmp     (r1)

        .globl  _increment_digits
_increment_digits:
        push    fp
        push    r2
        push    r1
        mov     fp,sp
        add     sp,-3
        lw      r0,12(fp)
        lc      r1,1
        sub     r0,r1
        sw      r0,-3(fp)
L8:
        lw      r0,-3(fp)
        lc      r1,0
        cls     r0,r1
        mov     r0,c
        ceq     r0,z
        mov     r0,c
        ceq     r0,z
        brt     L10
        lw      r0,9(fp)
        lw      r1,-3(fp)
        add     r0,r1
        lbu     r0,0(r0)
        lc      r1,57
        ceq     r0,r1
        mov     r0,c
        ceq     r0,z
        brt     L10
        lc      r0,1
        bra     L11
L10:
        lc      r0,0
L11:
        ceq     r0,z
        brt     L9
        lw      r0,9(fp)
        lw      r1,-3(fp)
        add     r0,r1
        mov     r1,r0
        lc      r0,48
        sb      r0,0(r1)
        lw      r0,-3(fp)
        lc      r1,1
        sub     r0,r1
        sw      r0,-3(fp)
        la      r2,L8
        jmp     (r2)
L9:
        lw      r0,-3(fp)
        cls     r0,z
        brt     L13
        lw      r0,9(fp)
        lw      r1,-3(fp)
        add     r0,r1
        lbu     r0,0(r0)
        lc      r1,1
        add     r0,r1
        push    r0
        lw      r0,9(fp)
        lw      r1,-3(fp)
        add     r0,r1
        mov     r1,r0
        pop     r0
        sb      r0,0(r1)
        lw      r0,12(fp)
        la      r2,L7
        jmp     (r2)
L13:
        lw      r0,12(fp)
        lc      r1,7
        cls     r0,r1
        brt     L19
        la      r2,L15
        jmp     (r2)
L19:
        lw      r0,12(fp)
        sw      r0,-3(fp)
L16:
        lw      r0,-3(fp)
        lc      r1,0
        cls     r1,r0
        brf     L18
        lw      r0,9(fp)
        push    r0
        lw      r0,-3(fp)
        lc      r1,1
        sub     r0,r1
        mov     r1,r0
        pop     r0
        add     r0,r1
        lbu     r0,0(r0)
        push    r0
        lw      r0,9(fp)
        lw      r1,-3(fp)
        add     r0,r1
        mov     r1,r0
        pop     r0
        sb      r0,0(r1)
L17:
        lw      r0,-3(fp)
        lc      r1,1
        sub     r0,r1
        sw      r0,-3(fp)
        bra     L16
L18:
        lw      r0,9(fp)
        lc      r1,0
        add     r0,r1
        mov     r1,r0
        lc      r0,49
        sb      r0,0(r1)
        lw      r0,12(fp)
        lc      r1,1
        add     r0,r1
        bra     L7
L15:
        lc      r0,1
L7:
        mov     sp,fp
        pop     r1
        pop     r2
        pop     fp
        jmp     (r1)

        .globl  _pause_between_numbers
_pause_between_numbers:
        push    fp
        push    r2
        push    r1
        mov     fp,sp
        add     sp,-3
        lc      r0,0
        sw      r0,-3(fp)
L21:
        lw      r0,-3(fp)
        la      r1,250000
        cls     r0,r1
        brf     L23
        nop
L22:
        lw      r0,-3(fp)
        lc      r1,1
        add     r0,r1
        sw      r0,-3(fp)
        bra     L21
L23:
L20:
        mov     sp,fp
        pop     r1
        pop     r2
        pop     fp
        jmp     (r1)

        .globl  _main
_main:
        push    fp
        push    r2
        push    r1
        mov     fp,sp
        add     sp,-11
        lc      r0,-8
        add     r0,fp
        lc      r1,0
        add     r0,r1
        mov     r1,r0
        lc      r0,48
        sb      r0,0(r1)
        lc      r0,1
        sw      r0,-11(fp)
L25:
        lc      r0,1
        ceq     r0,z
        brt     L26
        lw      r0,-11(fp)
        push    r0
        lc      r0,-8
        add     r0,fp
        push    r0
        la      r0,_print_digits
        jal     r1,(r0)
        add     sp,6
        lw      r0,-11(fp)
        push    r0
        lc      r0,-8
        add     r0,fp
        push    r0
        la      r0,_increment_digits
        jal     r1,(r0)
        add     sp,6
        sw      r0,-11(fp)
        la      r0,_pause_between_numbers
        jal     r1,(r0)
        bra     L25
L26:
L24:
        mov     sp,fp
        pop     r1
        pop     r2
        pop     fp
        jmp     (r1)
