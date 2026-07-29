/* Test round trip on loading and printing PI */
#include <libc24.h>
#include <stdio.h>

/* PI to 34 decimal digits (from Maple) */
static char pi34[] = "3.141592653589793238462643383279503";

int main()
{
    char c, *cp;
    int i;
    QUADFP p10, qd[10], qs, w10;

    /* Initialize quadruples 0-9 */
    i = 0;
    while (i < 10) {
        qd[i] = _qcvti(i);
        ++i;
    }

    /* Convert string to quadruple */
    cp = pi34;
    qs = _qcvti(0);
    w10 = _qcvti(10);
    p10 = w10;
    while (c = *cp++) {
        if (c == '.') {
            continue;
        }

        p10 = _qdiv(p10, w10);
        i = c - '0';
        qs = _qadd(qs, _qmul(qd[i], p10));
    }

    /* Print */
    printf("COR24: %.34wg\n", qs);

    return 0;
}
