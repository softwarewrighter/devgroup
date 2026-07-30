; Resident monitor for native PL/SW demonstrations and one shared SNOBOL4.
;
; Layout:
;   0x000000  existing SNOBOL4 native image
;   0x0B0000  embedded SNOBOL source selections
;   0x0D0000  this monitor
;   0x0D4000  PL/SW hello
;   0x0D8000  PL/SW counted-loop demo
;   0x0E0000  selected SNOBOL source
;   0x0F0000  empty input area selects live UART input
;
; Shared RX state:
;   0x0D3E00  head byte
;   0x0D3E03  tail byte
;   0x0D3E06  discard one leading menu CR/LF
;   0x0D3E09  Ctrl-] attention flag
;   0x0D3E10  256-byte ring

_start:
monitor_abort:
        ; Abandon any application stack and restore the EBR stack.
        la      r0,16706560
        push    r0
        pop     fp
        mov     sp,fp

        ; Menu polls directly with RX interrupts disabled.
        la      r1,-65520
        lc      r0,0
        sb      r0,0(r1)

        la      r0,banner
        la      r2,uart_puts
        jal     r1,(r2)

menu:
        la      r0,menu_text
        la      r2,uart_puts
        jal     r1,(r2)
menu_wait:
        la      r0,-65280
        lbu     r1,1(r0)
        lcu     r2,1
        and     r1,r2
        ceq     r1,z
        brt     menu_wait
        lbu     r1,0(r0)
        mov     r0,r1
        lcu     r2,10
        ceq     r0,r2
        brt     menu
        lcu     r2,13
        ceq     r0,r2
        brt     menu

        push    r1
        mov     r0,r1
        la      r2,uart_putc
        jal     r1,(r2)
        lcu     r0,10
        la      r2,uart_putc
        jal     r1,(r2)
        pop     r1

        mov     r0,r1
        lcu     r2,49
        ceq     r0,r2
        brt     launch_hello
        lcu     r2,50
        ceq     r0,r2
        brt     launch_loop
        lcu     r2,51
        ceq     r0,r2
        brt     launch_eliza
        lcu     r2,52
        ceq     r0,r2
        brt     launch_echo
        lcu     r2,53
        ceq     r0,r2
        brt     launch_palindrome
        lcu     r2,104
        ceq     r0,r2
        brt     show_help
        lcu     r2,72
        ceq     r0,r2
        brt     show_help
        lcu     r2,63
        ceq     r0,r2
        brt     show_help
        lcu     r2,113
        ceq     r0,r2
        brf     not_q_lower
        la      r0,monitor_halt
        jmp     (r0)
not_q_lower:
        lcu     r2,81
        ceq     r0,r2
        brf     not_q_upper
        la      r0,monitor_halt
        jmp     (r0)
not_q_upper:
        la      r0,unknown_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,menu
        jmp     (r0)

show_help:
        la      r0,help_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,menu
        jmp     (r0)

launch_hello:
        la      r0,hello_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,868352       ; 0x0D4000
        bra     launch_app

launch_loop:
        la      r0,loop_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,884736       ; 0x0D8000
        bra     launch_app

launch_eliza:
        la      r0,eliza_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,720896       ; 0x0B0000
        bra     launch_snobol

launch_echo:
        la      r0,echo_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,724992       ; 0x0B1000
        bra     launch_snobol

launch_palindrome:
        la      r0,pal_text
        la      r2,uart_puts
        jal     r1,(r2)
        la      r0,729088       ; 0x0B2000

launch_snobol:
        ; The sparse LGO omits complete all-zero SNOBOL records. Clear
        ; exactly those generated ranges before every interpreter launch.
        push    r0
        la      r0,clear_snobol_zeros
        jal     r1,(r0)
        pop     r0

        ; Copy selected zero-terminated source to SNOBOL's fixed source area.
        la      r1,917504       ; 0x0E0000
copy_source:
        lbu     r2,0(r0)
        sb      r2,0(r1)
        ceq     r2,z
        brt     source_ready
        add     r0,1
        add     r1,1
        bra     copy_source
source_ready:
        ; Empty data input forces SNOBOL INPUT to use live UART.
        la      r1,983040       ; 0x0F0000
        lc      r2,0
        sb      r2,0(r1)
        lc      r0,0            ; SNOBOL _start

launch_app:
        push    r0
        la      r1,867840       ; 0x0D3E00 head
        lc      r0,0
        sb      r0,0(r1)
        la      r1,867843       ; 0x0D3E03 tail
        sb      r0,0(r1)
        la      r1,867846       ; 0x0D3E06 skip menu newline
        lc      r0,1
        sb      r0,0(r1)
        la      r1,867849       ; 0x0D3E09 attention flag
        lc      r0,0
        sb      r0,0(r1)
        la      r0,uart_isr
        mov     iv,r0
        la      r1,-65520       ; UART RX interrupt enable
        lc      r0,1
        sb      r0,0(r1)
        pop     r0
        jmp     (r0)

; Clear the half-open [start,end) ranges generated from omitted L records.
; This preserves initialized constants in mixed/nonzero records and makes a
; second SNOBOL menu launch start with clean arenas as well.
clear_snobol_zeros:
        push    r1
        la      r0,snobol_zero_ranges
clear_range_next:
        push    r0
        lw      r1,0(r0)
        lw      r2,3(r0)
        la      r0,16777215
        ceq     r0,r1
        brt     clear_range_done
        mov     r0,r1
        lc      r1,0
clear_range_bytes:
        ceq     r0,r2
        brt     clear_range_advance
        sb      r1,0(r0)
        add     r0,1
        bra     clear_range_bytes
clear_range_advance:
        pop     r0
        add     r0,6
        bra     clear_range_next
clear_range_done:
        pop     r0
        pop     r1
        jmp     (r1)

monitor_halt:
        la      r0,halt_text
        la      r2,uart_puts
        jal     r1,(r2)
halt_loop:
        bra     halt_loop

; Read/acknowledge every byte. Ctrl-] changes IR so jmp(ir) clears the
; interrupt-in-service latch and resumes at the monitor recovery entry.
uart_isr:
        push    r0
        push    r1
        push    r2
        push    fp
        mov     r2,c
        push    r2
        la      r1,-65280
        lbu     r0,0(r1)
        lcu     r1,29
        ceq     r0,r1
        brt     isr_attention
        push    r0
        la      r1,867840
        lbu     r2,0(r1)
        mov     r0,r2
        add     r0,1
        lcu     r1,255
        and     r0,r1
        la      r1,867843
        lbu     r1,0(r1)
        ceq     r0,r1
        brt     isr_drop
        la      r1,867856       ; 0x0D3E10 ring
        add     r1,r2
        pop     r2
        sb      r2,0(r1)
        la      r1,867840
        sb      r0,0(r1)
        bra     isr_done
isr_drop:
        pop     r0
        bra     isr_done
isr_attention:
        ; COR24 cannot rewrite or read ir. Record attention and return through
        ; the original ir so hardware clears intis. The SNOBOL input broker
        ; performs the non-local monitor jump outside interrupt context.
        la      r1,867849
        lc      r0,1
        sb      r0,0(r1)
isr_done:
        pop     r2
        clu     z,r2
        pop     fp
        pop     r2
        pop     r1
        pop     r0
        jmp     (ir)

; SNOBOL-only patched input target. Standard PL/SW calling convention:
; r1 is the link; return byte in r0 while preserving r1/r2.
_MONITOR_GETCHAR:
        push    r1
        push    r2
monitor_getchar_wait:
        la      r0,867849
        lbu     r0,0(r0)
        ceq     r0,z
        brt     monitor_getchar_poll
        la      ir,monitor_abort
monitor_getchar_poll:
        la      r0,867840
        lbu     r0,0(r0)
        la      r2,867843
        lbu     r2,0(r2)
        ceq     r0,r2
        brt     monitor_getchar_wait
        la      r0,867856
        add     r0,r2
        lbu     r0,0(r0)
        add     r2,1
        push    r0
        la      r0,867843
        sb      r2,0(r0)
        pop     r0
        ; A line-oriented terminal normally sends CR/LF after the menu
        ; selection. Do not expose that terminator as SNOBOL's first line.
        la      r2,867846
        lbu     r2,0(r2)
        ceq     r2,z
        brt     monitor_getchar_return
        lcu     r2,13
        ceq     r0,r2
        brt     monitor_getchar_wait
        lcu     r2,10
        ceq     r0,r2
        brt     monitor_getchar_wait
        la      r2,867846
        push    r0
        lc      r0,0
        sb      r0,0(r2)
        pop     r0
monitor_getchar_return:
        pop     r2
        pop     r1
        jmp     (r1)

uart_puts:
        push    r1
uart_puts_loop:
        lbu     r1,0(r0)
        ceq     r1,z
        brt     uart_puts_done
        push    r0
        mov     r0,r1
        la      r2,uart_putc
        jal     r1,(r2)
        pop     r0
        add     r0,1
        bra     uart_puts_loop
uart_puts_done:
        pop     r1
        jmp     (r1)

uart_putc:
        push    r1
        push    r0
uart_putc_wait:
        la      r1,-65280
        lb      r2,1(r1)
        cls     r2,z
        brt     uart_putc_wait
        pop     r0
        sb      r0,0(r1)
        pop     r1
        jmp     (r1)

banner:
        .byte 10,67,79,82,50,52,32,80,76,47,83,87,32,97,110,100,32,83,78,79,66,79,76,52,32,100,101,109,111,115,10,0
menu_text:
        .byte 10,49,58,32,80,76,47,83,87,32,104,101,108,108,111,10,50,58,32,80,76,47,83,87,32,99,111,117,110,116,32,49,46,46,49,48,10,51,58,32,83,78,79,66,79,76,52,32,69,76,73,90,65,10,52,58,32,83,78,79,66,79,76,52,32,105,110,116,101,114,97,99,116,105,118,101,32,101,99,104,111,10,53,58,32,83,78,79,66,79,76,52,32,112,97,108,105,110,100,114,111,109,101,10,104,58,32,104,101,108,112,10,113,58,32,113,117,105,116,10,109,111,110,62,32,0
help_text:
        .byte 83,78,79,66,79,76,52,32,97,112,112,115,32,114,101,97,100,32,108,105,118,101,32,85,65,82,84,46,32,67,116,114,108,45,93,32,114,101,116,117,114,110,115,32,116,111,32,116,104,105,115,32,109,101,110,117,46,10,0
unknown_text:
        .byte 63,32,117,110,107,110,111,119,110,32,115,101,108,101,99,116,105,111,110,10,0
hello_text:
        .byte 83,116,97,114,116,105,110,103,32,80,76,47,83,87,32,104,101,108,108,111,46,10,0
loop_text:
        .byte 83,116,97,114,116,105,110,103,32,80,76,47,83,87,32,99,111,117,110,116,46,10,0
eliza_text:
        .byte 83,116,97,114,116,105,110,103,32,69,76,73,90,65,46,32,84,121,112,101,32,67,116,114,108,45,93,32,116,111,32,114,101,116,117,114,110,46,10,0
echo_text:
        .byte 83,116,97,114,116,105,110,103,32,83,78,79,66,79,76,52,32,101,99,104,111,46,32,84,121,112,101,32,67,116,114,108,45,93,32,116,111,32,114,101,116,117,114,110,46,10,0
pal_text:
        .byte 83,116,97,114,116,105,110,103,32,83,78,79,66,79,76,52,32,112,97,108,105,110,100,114,111,109,101,46,32,84,121,112,101,32,67,116,114,108,45,93,32,116,111,32,114,101,116,117,114,110,46,10,0
halt_text:
        .byte 77,111,110,105,116,111,114,32,104,97,108,116,101,100,46,10,0
