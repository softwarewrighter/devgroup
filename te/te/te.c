/* Simple terminal emulator */
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <memory.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/* COM, TTY devices */
#define COMDEVICE0 "/dev/ttyUSB0"
#define COMDEVICE1 "/dev/ttyUSB1"
#define TTYDEVICE "/dev/tty"

/* Log (to stderr) switch */
static int log;

/* Child process i.d. */
static int pid;

/* COM, TTY state */
static int fdcom, fdtty;
static struct termios comattr;
static struct termios oldcomattr;
static struct termios ttyattr;
static struct termios oldttyattr;

/*
 * Some USB UARTs (notably devices exposed through cdc_acm) accept CRTSCTS in
 * termios but do not necessarily perform transmit gating in the kernel.  Keep
 * CRTSCTS enabled, and also honor the adapter's reported CTS state here.
 */
static void wait_for_cts()
{
    int modem;

    for (;;) {
        if (ioctl(fdcom, TIOCMGET, &modem) < 0) {
            error(1, errno, "Couldn't read serial modem status");
        }
        if (modem & TIOCM_CTS) {
            return;
        }
        usleep(1000);
    }
}

/* Break (^C) handling */
void (*oldbrk)(int);
static void ourbrk(int signum)
{
    (void)signum;
    if (pid) {

        /* Parent kills child */
        kill(pid, SIGTERM);
        wait(NULL);

        /* Restore and close COM, TTY */
        tcsetattr(fdcom, TCSANOW, &oldcomattr);
        close(fdcom);
        tcsetattr(fdtty, TCSANOW, &oldttyattr);
        close(fdtty);

        /* Restore break handler */
        signal(SIGINT, oldbrk);

        /* Parent exits */
        exit(0);
    }
}

/* Read and write from TTY and COM */
char comget()
{
    char c;

    c = '\0';
    read(fdcom, &c, 1);

    return c;
}
char ttyget()
{
    char c;

    c = '\0';
    read(fdtty, &c, 1);

    return c;
}
void comput(c)
char c;
{
    ssize_t written;

    wait_for_cts();
    do {
        written = write(fdcom, &c, 1);
    } while (written < 0 && errno == EINTR);
    if (written != 1) {
        error(1, written < 0 ? errno : EIO, "Couldn't write serial byte");
    }
}
void ttyput(c)
char c;
{
    write(fdtty, &c, 1);
    if (log) {
        fputc((int)c, stderr);
    }
}
void ttyputs(s)
char *s;
{
    write(fdtty, s, strlen(s));
}

/* Open file for reading, in session */
static FILE *rfopen()
{
    char ttyin, fnam[80];
    size_t i;
    FILE *filefp;

    /* Get file name from TTY input */
    strcpy(fnam, "file: ");
    ttyputs(fnam);
    i = 0;
    while (i < (sizeof(fnam) - 1)) {
        ttyin = ttyget();
        if (ttyin == '\n') {
            break;
        }
        if (ttyin == 0x08) {
            if (i) {
                ttyput(0x08);
                ttyput(0x20);
                ttyput(0x08);
                --i;
            }
            continue;
        }
        ttyput(ttyin);
        fnam[i] = ttyin;
        ++i;
    }
    ttyput('\n');
    fnam[i] = '\0';

    if (!(filefp = fopen(fnam, "r"))) {
        printf("Couldn't open '%s' for reading\n", fnam);
    }

    return filefp;
}

/* Usage message */
static int usage()
{
    fprintf(stderr, "usage: te [-l] [serial-device]\n");
    exit(1);
}

int main(argc, argv)
int argc;
char *argv[];
{
    char ttyin, comin;
    char *comdevice;
    char *default_devices[] = {COMDEVICE0, COMDEVICE1};
    int filein;
    int argi;
    int default_index;
    int device_set;
    FILE *filefp;

    /* Get options */
    log = 0;
    comdevice = NULL;
    device_set = 0;
    for (argi = 1; argi < argc; ++argi) {
        if (!strcmp(argv[argi], "-l")) {
            log = 1;
        } else if (!device_set) {
            comdevice = argv[argi];
            device_set = 1;
        } else {
            usage();
        }
    }

    /* Hook ^C interrupt */
    oldbrk = signal(SIGINT, ourbrk);

    /* Open COM, TTY */
    filefp = NULL;
    if (device_set) {
        fdcom = open(comdevice, O_RDWR | O_NOCTTY);
    } else {
        fdcom = -1;
        for (default_index = 0; default_index < 2; ++default_index) {
            comdevice = default_devices[default_index];
            fdcom = open(comdevice, O_RDWR | O_NOCTTY);
            if (fdcom >= 0) {
                break;
            }
        }
    }
    if (fdcom < 0) {
        if (device_set) {
            error(1, errno, "Couldn't open serial device '%s'", comdevice);
        }
        error(1, errno, "Couldn't open /dev/ttyUSB0 or /dev/ttyUSB1");
    }
    if ((fdtty = open(TTYDEVICE, O_RDWR)) < 0) {
        error(1, errno, "Couldn't open TTY\n");
    }

    /* COM - raw mode, RTS/CTS, ignore break, and set speed */
    comin = '\0';
    tcgetattr(fdcom, &oldcomattr);
    memcpy(&comattr, &oldcomattr, sizeof(oldcomattr));
    cfmakeraw(&comattr);
    comattr.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    comattr.c_cflag |= CS8 | CREAD | CLOCAL | CRTSCTS;
    comattr.c_iflag |= IGNBRK;
    cfsetispeed(&comattr, B921600);
    cfsetospeed(&comattr, B921600);
    if (tcsetattr(fdcom, TCSANOW, &comattr) < 0) {
        error(1, errno, "Couldn't configure serial device '%s'", comdevice);
    }

    /* Assert adapter RTS, permitting the COR24 CTSN input to transmit. */
    {
        int bit = TIOCM_RTS;
        if (ioctl(fdcom, TIOCMBIS, &bit) < 0) {
            error(1, errno, "Couldn't assert RTS on '%s'", comdevice);
        }
    }
    fprintf(stderr, "serial: %s at 921600 8N1 with RTS/CTS\n", comdevice);

    /* TTY - make it character by character, we will echo ourselves */
    ttyin = '\0';
    tcgetattr(fdtty, &oldttyattr);
    memcpy(&ttyattr, &oldttyattr, sizeof(oldttyattr));
    ttyattr.c_lflag &= ~(ICANON | ECHO);
    ttyattr.c_cc[VERASE] = 0x08;
    tcsetattr(fdtty, TCSANOW, &ttyattr);

    /* Two processes */
    pid = fork();
    if (pid < 0) {
        exit(1);
    }

    /* TTY in to COM out and COM in to TTY out */
    if (!pid) {

        /* Child handles input from COM */
        while (1) {
            comin = comget();

            /* Local backspace/delete */
            if (comin == 0x08) {
                ttyput(0x08);
                ttyput(0x20);
            }

            ttyput(comin);
        }
    } else {

        /* Parent handles input from TTY (or file) */
        while (1) {
            if (!filefp) {
                ttyin = ttyget();

                /* Open file for input ? */
                if (ttyin == 0x12 /* ^R */) {
                    filefp = rfopen();
                    continue;
                } else {
                    comput(ttyin);
                }
            } else {
                filein = fgetc(filefp);
                if (!(filein == EOF)) {
                    ttyin = (char)filein;
                    comput(ttyin);
                } else {
                    fclose(filefp);
                    filefp = NULL;
                }
            }
        }
    }

    return 0;
}
