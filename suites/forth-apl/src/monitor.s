; COR24 resident UART-interrupt monitor for relocated Forth and APL.
;
; Fixed shared ABI:
;   0x000800  RX head byte (ISR producer)
;   0x000803  RX tail byte (application consumer)
;   0x000810  256-byte RX ring
;   0x001000  APL _start
;   0x020000  Forth _start

_start:
monitor_abort:
        ; An interrupt abort abandons the application's EBR stack. Restore
        ; the hardware 3 KiB stack top before doing any monitor work.
        la      r0, 16706560
        push    r0
        pop     fp
        mov     sp, fp

        ; Menu mode polls UART directly, with interrupts disabled.
        la      r1, -65520
        lc      r0, 0
        sb      r0, 0(r1)
        la      r1, 2054         ; clear abort flag
        sb      r0, 0(r1)

        la      r0, banner
        la      r2, uart_puts
        jal     r1, (r2)

menu:
        la      r0, menu_text
        la      r2, uart_puts
        jal     r1, (r2)

menu_wait:
        la      r0, -65280
        lbu     r1, 1(r0)
        lcu     r2, 1
        and     r1, r2
        ceq     r1, z
        brt     menu_wait
        lbu     r1, 0(r0)

        ; Ignore CR/LF left by line-oriented terminal use.
        mov     r0, r1
        lcu     r2, 10
        ceq     r0, r2
        brt     menu
        lcu     r2, 13
        ceq     r0, r2
        brt     menu

        ; Echo the menu selection.
        push    r1
        mov     r0, r1
        la      r2, uart_putc
        jal     r1, (r2)
        lcu     r0, 10
        la      r2, uart_putc
        jal     r1, (r2)
        pop     r1

        mov     r0, r1
        lcu     r2, 49           ; '1'
        ceq     r0, r2
        brt     launch_forth
        lcu     r2, 50           ; '2'
        ceq     r0, r2
        brt     launch_apl
        lcu     r2, 104          ; 'h'
        ceq     r0, r2
        brt     show_help
        lcu     r2, 72           ; 'H'
        ceq     r0, r2
        brt     show_help
        lcu     r2, 63           ; '?'
        ceq     r0, r2
        brt     show_help
        lcu     r2, 113          ; 'q'
        ceq     r0, r2
        brt     monitor_halt
        lcu     r2, 81           ; 'Q'
        ceq     r0, r2
        brt     monitor_halt

        la      r0, unknown_text
        la      r2, uart_puts
        jal     r1, (r2)
        bra     menu

show_help:
        la      r0, help_text
        la      r2, uart_puts
        jal     r1, (r2)
        bra     menu

launch_forth:
        la      r0, forth_text
        la      r2, uart_puts
        jal     r1, (r2)
        la      r0, 131072
        bra     launch_app

launch_apl:
        la      r0, apl_text
        la      r2, uart_puts
        jal     r1, (r2)
        la      r0, 4096

launch_app:
        ; Save the selected entry while initializing the broker.
        push    r0
        la      r1, 2048
        lc      r0, 0
        sb      r0, 0(r1)
        la      r1, 2051
        sb      r0, 0(r1)
        la      r1, 2054
        sb      r0, 0(r1)

        la      r0, uart_isr
        mov     iv, r0
        la      r1, -65520
        lc      r0, 1
        sb      r0, 0(r1)
        pop     r0
        jmp     (r0)

monitor_halt:
        la      r0, halt_text
        la      r2, uart_puts
        jal     r1, (r2)
halt_loop:
        bra     halt_loop

; UART RX ISR. All ordinary bytes enter the ring. Ctrl-] is monitor attention
; and abandons the interrupted application without exposing 0x1D to it.
uart_isr:
        push    r0
        push    r1
        push    r2
        push    fp
        mov     r2, c
        push    r2

        la      r1, -65280
        lbu     r0, 0(r1)       ; read acknowledges RX interrupt

        lcu     r1, 29
        ceq     r0, r1
        brt     isr_attention

        push    r0              ; preserve received byte
        ; next = (head + 1) & 0xff. Byte storage supplies the wrap.
        la      r1, 2048
        lbu     r2, 0(r1)       ; r2 = current head
        mov     r0, r2
        add     r0, 1           ; r0 = next head
        lcu     r1, 255
        and     r0, r1
        la      r1, 2051
        lbu     r1, 0(r1)
        ceq     r0, r1
        brt     isr_drop        ; full: deterministically drop newest byte

        la      r1, 2064
        add     r1, r2
        pop     r2              ; received byte
        sb      r2, 0(r1)
        la      r1, 2048
        sb      r0, 0(r1)
        bra     isr_done

isr_drop:
        pop     r0              ; discard received byte

isr_done:
        pop     r2
        clu     z, r2           ; restore condition flag
        pop     fp
        pop     r2
        pop     r1
        pop     r0
        jmp     (ir)

isr_attention:
        ; Record monitor attention, then return normally through ir so the
        ; CPU clears its interrupt-in-service latch. The application's
        ; broker observes this flag and abandons its stack at monitor entry.
        la      r1, 2054
        lc      r0, 1
        sb      r0, 0(r1)
        bra     isr_done

; r0 = zero-terminated byte string, r1 = link
uart_puts:
        push    r1
uart_puts_loop:
        lbu     r1, 0(r0)
        ceq     r1, z
        brt     uart_puts_done
        push    r0
        mov     r0, r1
        la      r2, uart_putc
        jal     r1, (r2)
        pop     r0
        add     r0, 1
        bra     uart_puts_loop
uart_puts_done:
        pop     r1
        jmp     (r1)

; r0 = byte, r1 = link
uart_putc:
        push    r1
        push    r0
uart_putc_wait:
        la      r1, -65280
        lb      r2, 1(r1)
        cls     r2, z
        brt     uart_putc_wait
        pop     r0
        sb      r0, 0(r1)
        pop     r1
        jmp     (r1)

banner:
        .byte 10,67,79,82,50,52,32,70,111,114,116,104,47,65,80,76,32,105,110,116,101,114,114,117,112,116,32,109,111,110,105,116,111,114,10,0
menu_text:
        .byte 10,49,58,32,70,111,114,116,104,10,50,58,32,65,80,76,10,104,58,32,104,101,108,112,10,113,58,32,113,117,105,116,10,109,111,110,62,32,0
help_text:
        .byte 49,32,115,116,97,114,116,115,32,70,111,114,116,104,59,32,50,32,115,116,97,114,116,115,32,65,80,76,46,10,67,116,114,108,45,93,32,105,110,116,101,114,114,117,112,116,115,32,101,105,116,104,101,114,32,97,112,112,32,97,110,100,32,114,101,116,117,114,110,115,32,104,101,114,101,46,10,0
unknown_text:
        .byte 63,32,117,110,107,110,111,119,110,32,115,101,108,101,99,116,105,111,110,10,0
forth_text:
        .byte 83,116,97,114,116,105,110,103,32,70,111,114,116,104,46,46,46,10,0
apl_text:
        .byte 83,116,97,114,116,105,110,103,32,65,80,76,46,46,46,10,0
abort_text:
        .byte 10,77,111,110,105,116,111,114,32,97,116,116,101,110,116,105,111,110,10,0
halt_text:
        .byte 77,111,110,105,116,111,114,32,104,97,108,116,101,100,59,32,112,114,101,115,115,32,83,49,32,102,111,114,32,108,111,97,100,45,97,110,100,45,103,111,46,10,0
