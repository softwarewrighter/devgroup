/* Checksummed second-stage load-and-go monitor for COR24-TB. */
#include <uartio.h>

#define MAXLINE 128
#define MAXDATA 32
#define LOADER_BASE 0x0fc000

static char line[MAXLINE];
static int expected_seq;
static int stream_crc;
static char finalized;

static char status()
{
    return *(UARTBASE + UARTSTAT);
}

static char getchr()
{
    while (!(status() & USTADRDY));
    return *(UARTBASE + UARTDATA);
}

static void putchr(c)
char c;
{
    while (!(status() & USTACTS));
    while (status() & USTABUSY);
    *(UARTBASE + UARTDATA) = c;
}

static void putstr(s)
register char *s;
{
    while (*s) putchr(*s++);
}

static void ready()
{
    putstr("TE2 READY 1\n");
}

static void puthex(value, digits)
register int value;
register int digits;
{
    register int shift;
    register int digit;

    shift = (digits - 1) * 4;
    while (digits--) {
        digit = (value >> shift) & 15;
        putchr(digit < 10 ? digit + '0' : digit - 10 + 'A');
        shift -= 4;
    }
}

static int hex(c)
register char c;
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int number(offset, digits, ok)
register int offset;
register int digits;
register char *ok;
{
    register int value;
    register int digit;

    value = 0;
    while (digits--) {
        digit = hex(line[offset++]);
        if (digit < 0) {
            *ok = 0;
            return 0;
        }
        value = (value << 4) | digit;
    }
    return value;
}

static int crcbyte(crc, byte)
register int crc;
register int byte;
{
    register int bit;

    crc ^= (byte & 255) << 8;
    bit = 8;
    while (bit--) {
        if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
        else crc <<= 1;
        crc &= 0xffff;
    }
    return crc;
}

static int record_crc(seq, address, count, data)
register int seq;
register int address;
register int count;
register char *data;
{
    register int crc;
    register int i;

    crc = 0xffff;
    crc = crcbyte(crc, seq >> 8);
    crc = crcbyte(crc, seq);
    crc = crcbyte(crc, address >> 16);
    crc = crcbyte(crc, address >> 8);
    crc = crcbyte(crc, address);
    crc = crcbyte(crc, count);
    i = 0;
    while (i < count) crc = crcbyte(crc, data[i++]);
    return crc;
}

static void ack(prefix, seq)
char prefix;
int seq;
{
    putchr(prefix);
    puthex(seq, 4);
    putchr('\n');
}

static void nack(seq, reason)
int seq;
char reason;
{
    putchr('N');
    puthex(seq, 4);
    putchr(reason);
    putchr('\n');
}

static int getline()
{
    register int length;
    register char c;

    length = 0;
    while (1) {
        c = getchr();
        if (c == '\r') continue;
        if (c == '\n') {
            line[length] = 0;
            return length;
        }
        if (length < MAXLINE - 1) line[length++] = c;
        else {
            while (getchr() != '\n');
            line[0] = 0;
            return -1;
        }
    }
}

static void data_record(length)
int length;
{
    char data[MAXDATA];
    char ok;
    register int seq;
    register int address;
    register int count;
    register int supplied_crc;
    register int calculated_crc;
    register int i;

    ok = 1;
    seq = number(1, 4, &ok);
    address = number(5, 6, &ok);
    count = number(11, 2, &ok);
    if (!ok || count > MAXDATA || length != 17 + count * 2) {
        nack(seq, 'F');
        return;
    }
    i = 0;
    while (i < count) {
        data[i] = number(13 + i * 2, 2, &ok);
        ++i;
    }
    supplied_crc = number(13 + count * 2, 4, &ok);
    if (!ok) {
        nack(seq, 'H');
        return;
    }
    calculated_crc = record_crc(seq, address, count, data);
    if (calculated_crc != supplied_crc) {
        nack(seq, 'C');
        return;
    }
    if (seq == ((expected_seq - 1) & 0xffff)) {
        ack('A', seq);              /* Lost ACK: safe duplicate. */
        return;
    }
    if (seq != expected_seq) {
        nack(seq, 'S');
        return;
    }
    if (address < 0 || address + count > LOADER_BASE) {
        nack(seq, 'R');
        return;
    }
    i = 0;
    while (i < count) {
        ((char *)address)[i] = data[i];
        ++i;
    }
    stream_crc = crcbyte(stream_crc, seq >> 8);
    stream_crc = crcbyte(stream_crc, seq);
    stream_crc = crcbyte(stream_crc, address >> 16);
    stream_crc = crcbyte(stream_crc, address >> 8);
    stream_crc = crcbyte(stream_crc, address);
    stream_crc = crcbyte(stream_crc, count);
    i = 0;
    while (i < count) {
        if (((char *)address)[i] != data[i]) {
            nack(seq, 'M');
            return;
        }
        ++i;
    }
    i = 0;
    while (i < count) {
        stream_crc = crcbyte(stream_crc, ((char *)address)[i]);
        ++i;
    }
    expected_seq = (expected_seq + 1) & 0xffff;
    finalized = 0;
    ack('A', seq);
}

static void end_record(length)
int length;
{
    char ok;
    register int seq;
    register int crc;

    ok = 1;
    seq = number(1, 4, &ok);
    crc = number(5, 4, &ok);
    if (!ok || length != 9 || seq != expected_seq || crc != stream_crc) {
        nack(seq, 'E');
        return;
    }
    finalized = 1;
    ack('F', seq);
}

static void go_record(length)
int length;
{
    char ok;
    register int address;

    ok = 1;
    address = number(1, 6, &ok);
    if (!ok || length != 7 || !finalized || address >= LOADER_BASE) {
        nack(expected_seq, 'G');
        return;
    }
    putchr('J');
    puthex(address, 6);
    putchr('\n');
    while (status() & USTABUSY);
    ((int (*)())address)();
}

int main()
{
    register int length;

    expected_seq = 0;
    stream_crc = 0xffff;
    finalized = 0;
    ready();
    while (1) {
        length = getline();
        if (length < 1) {
            nack(expected_seq, 'L');
        } else if (line[0] == 'D') {
            data_record(length);
        } else if (line[0] == 'E') {
            end_record(length);
        } else if (line[0] == 'G') {
            go_record(length);
        } else if (line[0] == 'H' && length == 1) {
            ready();
        } else {
            nack(expected_seq, '?');
        }
    }
    return 0;
}
