/* Verified, retrying two-stage uploader and terminal for COR24-TB. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_RETRIES 5
#define BOOTSTRAP_MS 250
#define DEFAULT_RESPONSE_MS 1000
#define BOOTSTRAP_TOTAL_MS 25000
#define MAX_RECORD_DATA 32
#define LOADER_BASE 0x0fc000UL

static int serial_fd = -1;
static struct termios saved_serial;
static int have_saved_serial;
static struct termios saved_stdin;
static int have_saved_stdin;
static long long bootstrap_deadline;
static int rx_fd = -1;
static pid_t reader_pid = -1;
static int response_ms = DEFAULT_RESPONSE_MS;
static unsigned record_data_size = MAX_RECORD_DATA;

static int wait_line(const char *expected, int timeout_ms);

static void restore(void)
{
    if (reader_pid > 0) {
        kill(reader_pid, SIGTERM);
        waitpid(reader_pid, NULL, 0);
        reader_pid = -1;
    }
    if (rx_fd >= 0) {
        close(rx_fd);
        rx_fd = -1;
    }
    if (have_saved_stdin) tcsetattr(STDIN_FILENO, TCSANOW, &saved_stdin);
    if (have_saved_serial) tcsetattr(serial_fd, TCSANOW, &saved_serial);
}

static void interrupted(int signal_number)
{
    (void)signal_number;
    exit(130);
}

static void die(const char *message)
{
    restore();
    fprintf(stderr, "te2: %s\n", message);
    exit(1);
}

static void syserr(const char *message)
{
    int saved = errno;
    restore();
    errno = saved;
    perror(message);
    exit(1);
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void write_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *next = buffer;
    while (length) {
        ssize_t count = write(fd, next, length);
        if (count > 0) {
            next += count;
            length -= (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd descriptor = {fd, POLLOUT, 0};
            int ready = poll(&descriptor, 1, response_ms);
            if (ready > 0) continue;
            if (ready < 0 && errno == EINTR) continue;
            errno = ETIMEDOUT;
            syserr("serial write timeout");
        } else {
            syserr("serial write");
        }
    }
}

static int wait_for_cts(int timeout_ms)
{
    long long deadline = now_ms() + timeout_ms;
    if (bootstrap_deadline && deadline > bootstrap_deadline)
        deadline = bootstrap_deadline;
    while (now_ms() < deadline) {
        int modem;
        struct timespec pause = {0, 1000000};
        if (ioctl(serial_fd, TIOCMGET, &modem) < 0)
            syserr("TIOCMGET");
        if (modem & TIOCM_CTS) return 1;
        nanosleep(&pause, NULL);
    }
    return 0;
}

static int write_serial_byte(unsigned char byte, int timeout_ms)
{
    long long deadline = now_ms() + timeout_ms;
    if (bootstrap_deadline && deadline > bootstrap_deadline)
        deadline = bootstrap_deadline;
    while (now_ms() < deadline) {
        struct pollfd descriptor = {serial_fd, POLLOUT, 0};
        long long remaining = deadline - now_ms();
        ssize_t count;

        if (poll(&descriptor, 1, (int)remaining) < 0) {
            if (errno == EINTR) continue;
            syserr("poll bootstrap write");
        }
        if (!(descriptor.revents & POLLOUT)) return 0;
        count = write(serial_fd, &byte, 1);
        if (count == 1) return 1;
        if (count < 0 && (errno == EINTR || errno == EAGAIN ||
                         errno == EWOULDBLOCK)) continue;
        if (count < 0) syserr("bootstrap write");
    }
    return 0;
}

static int send_flow_text(const char *text, int timeout_ms)
{
    while (*text) {
        if (!wait_for_cts(timeout_ms)) return 0;
        if (!write_serial_byte((unsigned char)*text, timeout_ms)) return 0;
        ++text;
    }
    return 1;
}

static void drain_until_quiet(int quiet_ms)
{
    unsigned char buffer[256];
    for (;;) {
        struct pollfd descriptor = {rx_fd, POLLIN, 0};
        int ready = poll(&descriptor, 1, quiet_ms);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || !(descriptor.revents & POLLIN)) return;
        if (read(rx_fd, buffer, sizeof(buffer)) < 0 && errno != EINTR &&
            errno != EAGAIN && errno != EWOULDBLOCK)
            syserr("drain bootstrap input");
    }
}

/* The reader process drains concurrently, as in the proven C te. */
static int bootstrap_line(const char *line)
{
    if (!send_flow_text(line, BOOTSTRAP_MS)) return 0;
    if (!send_flow_text("\n", BOOTSTRAP_MS)) return 0;
    return wait_line(line, response_ms);
}

static int read_line(char *line, size_t capacity, int timeout_ms)
{
    size_t length = 0;
    long long deadline = now_ms() + timeout_ms;

    for (;;) {
        struct pollfd descriptor = {rx_fd, POLLIN, 0};
        long long remaining = deadline - now_ms();
        unsigned char byte;
        ssize_t count;

        if (remaining <= 0) return 0;
        if (poll(&descriptor, 1, (int)remaining) < 0) {
            if (errno == EINTR) continue;
            syserr("poll serial");
        }
        if (!(descriptor.revents & POLLIN)) return 0;
        count = read(rx_fd, &byte, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count != 1) continue;
        if (byte == '\r') continue;
        if (byte == '\n') {
            line[length] = 0;
            return 1;
        }
        if (length + 1 < capacity) line[length++] = (char)byte;
    }
}

static int wait_line(const char *expected, int timeout_ms)
{
    char line[256];
    long long deadline = now_ms() + timeout_ms;

    while (now_ms() < deadline) {
        int remaining = (int)(deadline - now_ms());
        if (!read_line(line, sizeof(line), remaining)) return 0;
        if (!strcmp(line, expected)) return 1;
        if (*line) fprintf(stderr, "COR24: %s\n", line);
    }
    return 0;
}

static void configure_serial(int fd)
{
    struct termios attr;
    if (tcgetattr(fd, &saved_serial) < 0) syserr("tcgetattr serial");
    have_saved_serial = 1;
    attr = saved_serial;
    cfmakeraw(&attr);
    attr.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    attr.c_cflag |= CS8 | CREAD | CLOCAL | CRTSCTS;
    attr.c_iflag |= IGNBRK;
    if (cfsetispeed(&attr, B921600) < 0 ||
        cfsetospeed(&attr, B921600) < 0 ||
        tcsetattr(fd, TCSANOW, &attr) < 0) {
        syserr("configure serial");
    }
    {
        int bit = TIOCM_RTS;
        if (ioctl(fd, TIOCMBIS, &bit) < 0) syserr("assert RTS");
    }
    tcflush(fd, TCIFLUSH);
}

static void start_reader(void)
{
    int channel[2];
    if (pipe(channel) < 0) syserr("reader pipe");
    reader_pid = fork();
    if (reader_pid < 0) syserr("reader fork");
    if (reader_pid == 0) {
        unsigned char buffer[512];
        close(channel[0]);
        for (;;) {
            struct pollfd descriptor = {serial_fd, POLLIN, 0};
            ssize_t count;
            if (poll(&descriptor, 1, -1) < 0) {
                if (errno == EINTR) continue;
                _exit(1);
            }
            if (!(descriptor.revents & POLLIN)) continue;
            count = read(serial_fd, buffer, sizeof(buffer));
            if (count > 0) {
                const unsigned char *next = buffer;
                ssize_t left = count;
                while (left > 0) {
                    ssize_t written = write(channel[1], next, (size_t)left);
                    if (written > 0) {
                        next += written;
                        left -= written;
                    } else if (written < 0 && errno == EINTR) {
                        continue;
                    } else {
                        _exit(1);
                    }
                }
            }
        }
    }
    close(channel[1]);
    rx_fd = channel[0];
}

static int open_serial(const char *requested, const char **selected)
{
    static const char *defaults[] = {"/dev/ttyUSB0", "/dev/ttyUSB1"};
    int fd;
    size_t i;

    if (requested) {
        *selected = requested;
        return open(requested, O_RDWR | O_NOCTTY | O_NONBLOCK);
    }
    for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]); ++i) {
        fd = open(defaults[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd >= 0) {
            *selected = defaults[i];
            return fd;
        }
    }
    *selected = NULL;
    return -1;
}

static void loader_path(char *path, size_t capacity, const char *argv0)
{
    char executable[1024];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable) - 1);
    char *slash;

    if (length >= 0) {
        executable[length] = 0;
    } else {
        snprintf(executable, sizeof(executable), "%s", argv0);
    }
    slash = strrchr(executable, '/');
    if (slash) {
        *slash = 0;
        snprintf(path, capacity, "%s/te2.lgo", executable);
    } else {
        snprintf(path, capacity, "te2.lgo");
    }
}

static void strip_newline(char *line)
{
    size_t length = strlen(line);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = 0;
}

static void bootstrap(const char *path, int retries)
{
    FILE *file = fopen(path, "r");
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    unsigned number = 0;

    if (!file) syserr(path);
    bootstrap_deadline = now_ms() + BOOTSTRAP_TOTAL_MS;
    fprintf(stderr, "bootstrap: %s\n", path);
    while ((length = getline(&line, &capacity, file)) >= 0) {
        int attempt;
        int is_go;
        strip_newline(line);
        if (!*line) continue;
        ++number;
        is_go = line[0] == 'G';
        for (attempt = 1; attempt <= retries; ++attempt) {
            if (now_ms() >= bootstrap_deadline)
                die("bootstrap exceeded 25-second deadline");
            if (bootstrap_line(line)) break;
            if (is_go) break; /* Never resend a jump after losing its echo. */
            /* Terminate any partial ROM-monitor command, then discard its
               echo/error response before safely resending the absolute L. */
            {
                unsigned char newline = '\n';
                if (wait_for_cts(BOOTSTRAP_MS))
                    write_serial_byte(newline, BOOTSTRAP_MS);
                drain_until_quiet(50);
            }
            fprintf(stderr, "bootstrap line %u: retry %d/%d\n",
                    number, attempt, retries);
        }
        if (attempt > retries || (is_go && attempt > 1))
            die("bootstrap echo verification failed");
    }
    free(line);
    fclose(file);
    if (now_ms() >= bootstrap_deadline)
        die("bootstrap exceeded 25-second deadline");
    if (!wait_line("TE2 READY 1", 2000)) die("second-stage loader did not become ready");
    bootstrap_deadline = 0;
    fprintf(stderr, "bootstrap: loader ready after %u records\n", number);
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static unsigned long hex_number(const char *text, size_t digits, int *ok)
{
    unsigned long value = 0;
    size_t i;
    for (i = 0; i < digits; ++i) {
        int digit = hex_digit((unsigned char)text[i]);
        if (digit < 0) {
            *ok = 0;
            return 0;
        }
        value = (value << 4) | (unsigned)digit;
    }
    return value;
}

static unsigned crc_byte(unsigned crc, unsigned byte)
{
    int bit;
    crc ^= (byte & 255U) << 8;
    for (bit = 0; bit < 8; ++bit)
        crc = (crc & 0x8000U) ? (crc << 1) ^ 0x1021U : crc << 1;
    return crc & 0xffffU;
}

static unsigned record_crc(unsigned seq, unsigned long address,
                           const unsigned char *data, unsigned count)
{
    unsigned crc = 0xffffU;
    unsigned i;
    crc = crc_byte(crc, seq >> 8);
    crc = crc_byte(crc, seq);
    crc = crc_byte(crc, address >> 16);
    crc = crc_byte(crc, address >> 8);
    crc = crc_byte(crc, address);
    crc = crc_byte(crc, count);
    for (i = 0; i < count; ++i) crc = crc_byte(crc, data[i]);
    return crc;
}

static void update_stream_crc(unsigned *crc, unsigned seq,
                              unsigned long address,
                              const unsigned char *data, unsigned count)
{
    unsigned i;
    *crc = crc_byte(*crc, seq >> 8);
    *crc = crc_byte(*crc, seq);
    *crc = crc_byte(*crc, address >> 16);
    *crc = crc_byte(*crc, address >> 8);
    *crc = crc_byte(*crc, address);
    *crc = crc_byte(*crc, count);
    for (i = 0; i < count; ++i) *crc = crc_byte(*crc, data[i]);
}

static void send_record(unsigned seq, unsigned long address,
                        const unsigned char *data, unsigned count, int retries)
{
    char command[128];
    char expected[16];
    char *next;
    unsigned crc;
    unsigned i;
    int attempt;

    crc = record_crc(seq, address, data, count);
    next = command + snprintf(command, sizeof(command), "D%04X%06lX%02X",
                              seq, address, count);
    for (i = 0; i < count; ++i) next += sprintf(next, "%02X", data[i]);
    sprintf(next, "%04X", crc);
    snprintf(expected, sizeof(expected), "A%04X", seq);

    for (attempt = 1; attempt <= retries; ++attempt) {
        if (send_flow_text(command, response_ms) &&
            send_flow_text("\n", response_ms) &&
            wait_line(expected, response_ms)) return;
        fprintf(stderr, "record %u at %06lX: retry %d/%d\n",
                seq, address, attempt, retries);
    }
    die("record retry limit exceeded");
}

static void send_verified_image(const char *path, int retries,
                                unsigned long *entry_out)
{
    FILE *file = fopen(path, "r");
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    unsigned seq = 0;
    unsigned stream_crc = 0xffffU;
    unsigned long entry = 0;
    int have_entry = 0;

    if (!file) syserr(path);
    fprintf(stderr, "image: %s\n", path);
    while ((length = getline(&line, &capacity, file)) >= 0) {
        int ok = 1;
        unsigned long address;
        size_t hex_length;
        size_t offset;
        strip_newline(line);
        if (!*line || line[0] == ';') continue;
        if (line[0] == 'G') {
            if (strlen(line) != 7) die("bad G record in image");
            entry = hex_number(line + 1, 6, &ok);
            if (!ok || entry >= LOADER_BASE) die("invalid image entry address");
            have_entry = 1;
            continue;
        }
        if (line[0] != 'L' || strlen(line) < 9)
            die("unsupported record in image");
        address = hex_number(line + 1, 6, &ok);
        hex_length = strlen(line + 7);
        if (!ok || (hex_length & 1)) die("bad L record in image");
        offset = 0;
        while (offset < hex_length) {
            unsigned char data[MAX_RECORD_DATA];
            unsigned count = (unsigned)((hex_length - offset) / 2);
            unsigned i;
            if (count > record_data_size) count = record_data_size;
            if (address + count > LOADER_BASE)
                die("image overlaps second-stage loader");
            for (i = 0; i < count; ++i) {
                data[i] = (unsigned char)hex_number(line + 7 + offset + i * 2,
                                                    2, &ok);
            }
            if (!ok) die("bad hex data in image");
            send_record(seq, address, data, count, retries);
            update_stream_crc(&stream_crc, seq, address, data, count);
            seq = (seq + 1) & 0xffffU;
            address += count;
            offset += count * 2;
            if (!(seq % 100)) fprintf(stderr, "verified: %u records\n", seq);
        }
    }
    free(line);
    fclose(file);
    if (!have_entry) die("image has no G entry record");
    {
        char command[32];
        char expected[16];
        int attempt;
        snprintf(command, sizeof(command), "E%04X%04X", seq, stream_crc);
        snprintf(expected, sizeof(expected), "F%04X", seq);
        for (attempt = 1; attempt <= retries; ++attempt) {
            if (send_flow_text(command, response_ms) &&
                send_flow_text("\n", response_ms) &&
                wait_line(expected, response_ms)) break;
            fprintf(stderr, "final checksum: retry %d/%d\n", attempt, retries);
        }
        if (attempt > retries) die("final stream checksum failed");
    }
    fprintf(stderr, "verified: %u records, CRC16=%04X\n", seq, stream_crc);
    *entry_out = entry;
}

static void terminal_mode(void)
{
    struct termios raw;
    unsigned char buffer[1024];

    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &saved_stdin) < 0) syserr("tcgetattr stdin");
        have_saved_stdin = 1;
        raw = saved_stdin;
        cfmakeraw(&raw);
        raw.c_oflag |= OPOST;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) syserr("tcsetattr stdin");
    }
    for (;;) {
        struct pollfd descriptors[2] = {
            {rx_fd, POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}
        };
        int ready = poll(descriptors, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            syserr("terminal poll");
        }
        if (descriptors[0].revents & POLLIN) {
            ssize_t count = read(rx_fd, buffer, sizeof(buffer));
            if (count > 0) write_all(STDOUT_FILENO, buffer, (size_t)count);
        }
        if (descriptors[1].revents & POLLIN) {
            ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count > 0) write_all(serial_fd, buffer, (size_t)count);
        }
    }
}

static void usage(const char *name)
{
    fprintf(stderr,
            "usage: %s [-2] [-d device] [-L loader.lgo] [-r retries] "
            "[-t timeout-ms] [-n record-bytes] image.lgo\n",
            name);
    exit(1);
}

int main(int argc, char **argv)
{
    const char *requested = NULL;
    const char *selected;
    const char *image = NULL;
    int retries = DEFAULT_RETRIES;
    int stage2_only = 0;
    const char *loader_override = NULL;
    int option;
    char stage2[1200];
    unsigned long entry;
    char go[32];
    char jumped[32];

    while ((option = getopt(argc, argv, "2d:L:r:t:n:h")) != -1) {
        switch (option) {
        case '2': stage2_only = 1; break;
        case 'd': requested = optarg; break;
        case 'L': loader_override = optarg; break;
        case 'r':
            retries = atoi(optarg);
            if (retries < 1 || retries > 100) usage(argv[0]);
            break;
        case 't':
            response_ms = atoi(optarg);
            if (response_ms < 50 || response_ms > 10000) usage(argv[0]);
            break;
        case 'n':
            record_data_size = (unsigned)atoi(optarg);
            if (record_data_size < 1 || record_data_size > MAX_RECORD_DATA)
                usage(argv[0]);
            break;
        default: usage(argv[0]);
        }
    }
    if (optind + 1 != argc) usage(argv[0]);
    image = argv[optind];
    if (loader_override) snprintf(stage2, sizeof(stage2), "%s", loader_override);
    else loader_path(stage2, sizeof(stage2), argv[0]);

    serial_fd = open_serial(requested, &selected);
    if (serial_fd < 0) syserr(requested ? requested : "/dev/ttyUSB0 or /dev/ttyUSB1");
    atexit(restore);
    configure_serial(serial_fd);
    start_reader();
    signal(SIGINT, interrupted);
    signal(SIGTERM, interrupted);
    fprintf(stderr,
            "serial: %s at 921600 8N1 with RTS/CTS; retries=%d; "
            "timeout=%dms; record=%u bytes\n",
            selected, retries, response_ms, record_data_size);

    if (stage2_only) {
        drain_until_quiet(50);
        if (!send_flow_text("H\n", response_ms) ||
            !wait_line("TE2 READY 1", response_ms))
            die("no response from existing second-stage loader");
        fprintf(stderr, "stage2: attached to existing loader\n");
    } else {
        bootstrap(stage2, retries);
    }
    send_verified_image(image, retries, &entry);
    snprintf(go, sizeof(go), "G%06lX", entry);
    snprintf(jumped, sizeof(jumped), "J%06lX", entry);
    if (!send_flow_text(go, response_ms) ||
        !send_flow_text("\n", response_ms) ||
        !wait_line(jumped, response_ms))
        die("loader did not acknowledge jump");
    fprintf(stderr, "jump: %06lX; terminal active (Ctrl-C exits)\n", entry);
    terminal_mode();
    return 0;
}
