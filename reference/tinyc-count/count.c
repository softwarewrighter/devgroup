/*
 * Small COR24-TB UART counter.
 *
 * Prints:
 *   0
 *   1
 *   2
 *   ...
 *
 * It deliberately avoids stdio and division to keep the load image small.
 * The decimal digits are maintained as an ASCII odometer.
 */

#define UART_DATA   0xFF0100
#define UART_STATUS 0xFF0101
#define UART_TX_BUSY 0x80

void uart_putc(int ch) {
    while (*(char *)UART_STATUS & UART_TX_BUSY) {
    }
    *(char *)UART_DATA = ch;
}

void print_digits(char *digits, int length) {
    int i;

    for (i = 0; i < length; i = i + 1) {
        uart_putc(digits[i]);
    }
    uart_putc(13);
    uart_putc(10);
}

int increment_digits(char *digits, int length) {
    int i;

    i = length - 1;
    while (i >= 0 && digits[i] == '9') {
        digits[i] = '0';
        i = i - 1;
    }

    if (i >= 0) {
        digits[i] = digits[i] + 1;
        return length;
    }

    if (length < 7) {
        for (i = length; i > 0; i = i - 1) {
            digits[i] = digits[i - 1];
        }
        digits[0] = '1';
        return length + 1;
    }

    /* Wrap after 9,999,999, long after this smoke test is useful. */
    return 1;
}

void pause_between_numbers(void) {
    int i;

    /*
     * A visible hardware pace without timers. The emulator command runs at
     * unlimited speed, so its instruction limit controls how much it prints.
     */
    for (i = 0; i < 250000; i = i + 1) {
        asm("nop");
    }
}

int main() {
    char digits[8];
    int length;

    digits[0] = '0';
    length = 1;

    while (1) {
        print_digits(digits, length);
        length = increment_digits(digits, length);
        pause_between_numbers();
    }
}
