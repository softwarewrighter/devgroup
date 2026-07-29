/* Test UART interrupt service */
#include <intctlr.h>
#include <stdio.h>
#include <uartio.h>

/* Interrupt service routine */
extern void uartaisr();

/* Circular character buffer */
#define BUFSZ 16
static char cbuf[BUFSZ];
static int ridx, widx;

/* Called from assembly interrupt service routine */
void uartcisr()
{
    /* Put character in buffer, if there's room  */
    if (!(((widx + 1) % BUFSZ) == ridx)) {
        cbuf[widx] = *(UARTBASE + UARTDATA);
        widx = (widx + 1) % BUFSZ;
    } else {
        /* Disable UART interrupts */
        *(INTCBASE + INTCENAB) = 0;
    }
}

int main()
{
    char c;

    /* Initialize circular buffer */
    ridx = widx = 0;

    /* Set interrupt service routine */
    initivec(uartaisr);

    /* Echo characters received */
    while (1) {
        if (!(ridx == widx)) {
            c = cbuf[ridx];
            ridx = (ridx + 1) % BUFSZ;
            putchar(c);
        } else {
            /* Enable UART interrupts */
            *(INTCBASE + INTCENAB) = 1;
        }
    }
}
