; UART interrupt service

        .text
        .globl  _uartaisr
_uartaisr:
        push    fp
        push    r2
        push    r1
        push    r0
        mov     r0,c
        push    r0

        la      r0,_uartcisr
        jal     r1,(r0)         ; Call C routine

        pop     r0
        clu     z,r0
        pop     r0
        pop     r1
        pop     r2
        pop     fp
        jmp     (ir)

        .globl  _initivec
_initivec:
        push    fp
        mov     fp,sp

        lw      r0,3(fp)
        mov     iv,r0

        pop     fp
        jmp     (r1)
