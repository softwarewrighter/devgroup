/* Monitor - load program and go */
#include <ledswio.h>
#include <uartio.h>

/* Messages */
static char inimsg[] = "Load and go monitor\n";
static char badhex[] = "? Bad hex character:";
static char fmterr[] = "? Command format:";
static char jmpmsg[] = "Jump to ";
static char llnerr[] = "? Line too long\n";
static char unkerr[] = "? Unknown command:";

/* Hex table */
static char hextab[] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46
};

/* LED state */
static char toggled = 0xff;

/* Line buffer */
#define LINSIZ 81
char linbuf[LINSIZ];

/* Put character or string to UART */
static char putchr(c)
char c;
{
    register char *uart = UARTBASE;

    /* Wait for CTS */
    while (!(*(uart + UARTSTAT) & USTACTS));

    /* Wait until not busy (0x80) */
    while (*(uart + UARTSTAT) < 0);

    /* Write character */
    return *(uart + UARTDATA) = c;
}
static void putstr(s)
register char *s;
{
    while (*s) {
        putchr(*s);
        ++s;
    }
}

/* Get character */
static char getchr()
{
    register char c;

    while (!(*(UARTBASE + UARTSTAT) & USTADRDY));

    c = *(UARTBASE + UARTDATA);
    if (c == '\r') {
        c = '\n';
    }

    return c;
}

/* Get a command line */
static int getline()
{
    char c;
    register int i;

    i = 0;
    while (i < (LINSIZ - 1)) {
        linbuf[i] = (c = putchr(getchr()));
        ++i;
        if (c == '\n') {
            break;
        }
    }
    linbuf[i] = '\0';

    return c == '\n';
}

/* Convert character to hex numeral */
static int hexnum(c)
register unsigned char c;
{
    if (!(c < '0') && !('9' < c)) {
        return (c - '0');
    } else {
        if (!(c < 'A') && !('F' < c)) {
            return (c - 'A' + 10);
        }
    }
        
    putstr(badhex);
    putchr(c);
    putchr('\n');

    return -1;
}

/* Display a character in hex */
static dspbyte(c)
char c;
{
    register int i = 8;

    while (i) {
        i -= 4;
        putchr(hextab[(c >> i) & 0x0f]);
    }
}

/* Command routines */
static void dogo()
{
    register int c, i, jmpadr;

    jmpadr = 0;
    i = 1;
    while (1) {

        /* End of command line ? */
        c = linbuf[i++];
        if (c == '\n') {

            /* No address given ? */
            if (i == 2) {
noaddr:
                putstr(fmterr);
                putstr(linbuf);
                return;
            }

            /* Announce the jump */
            putstr(jmpmsg);
            c = 3;
            while (c--) {
                dspbyte(((char *)&jmpadr)[c]);
            }
            putchr('\n');

            /* Wait for output to complete */
            while (*(UARTBASE + UARTSTAT) < 0);

            /* Do the jump (call) */
            ((int (*)())jmpadr)();
            return;
        }

        /* Longer than 'Gaaaaaa' ? */
        if (i == 8) {
            goto noaddr;
        }

        /* Get hex numeral */
        if ((c = hexnum(c)) < 0) {
            return;
        }

        /* Assemble address */
        jmpadr = (jmpadr << 4) | c;
    }
}
static void doload()
{
    char lodnyb;
    register int c, i, lodadr;

    lodadr = lodnyb = 0;
    i = 1;
    while (1) {

        /* End of command line ? */
        c = linbuf[i++];
        if (c == '\n') {

            /* Shorter than 'Laaaaaadd' ? */
            /* Odd number of nybbles ? */
            if ((i < 10) || lodnyb) {
                putstr(fmterr);
                putstr(linbuf);
            }
            return;
        }

        /* Get hex numeral */
        if ((c = hexnum(c)) < 0) {
            return;
        }

        /* Still assembling address ? */
        if (i < 8) {
            lodadr = (lodadr << 4) | c;
        } else {

            /* Storing data nybbles now */
            if (!lodnyb) {

                /* Set byte to even nybble, high */
                *(char *)lodadr = (c <<= 4);
            } else {

                /* Or in odd nybble, low */
                c |= *(char *)lodadr;
                ((char *)++lodadr)[-1] = c;
            }

            /* Toggle nybble index */
            lodnyb = !lodnyb;
        }
    }
}

void main()
{
    /* Set UART baud rate to 921600 */
    *(UARTBASE + UARTBAUD) = B921600;

    /* Announce */
    putstr(inimsg);

    /* Get and execute commands */
    while (1) {

        /* Get command line */
        if (!getline()) {
            putstr(llnerr);
            continue;
        }

        /* Toggle LED to note line received */
        *LEDSWDAT = ++toggled;

        /* Do it */
        switch (linbuf[0]) {
            case ';' :
                break;
            case 'G' :
                dogo();
                break;
            case 'L' :
                doload();
                break;
            default :
                putstr(unkerr);
                putstr(linbuf);
                break;
        }
    }
}
