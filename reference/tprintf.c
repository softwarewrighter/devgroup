/* Test my printf implementation */
#include <stdio.h>

int main()
{
    double zero;
    union dfp {
        struct {
            int fr0 : 24;
            int fr1 : 24;
            int fr2 : 4;
            int exp : 11;
            int sgn : 1;
        } i;
        double f;
    } dx;

    /* Zero, NaN, Inf */
    zero = 0.0;
    printf("%g should be 0\n", zero);
    printf("%g should be -inf\n", -1.0 / zero);
    printf("%g should be -nan\n", 0.0 / zero);
    printf("\n");

    /* From Gnu test-printf */
    printf("%e should be 1.234568e+06\n", 1234567.8);
    printf("%f should be 1234567.800000\n", 1234567.8);
    printf("%g should be 1.23457e+06\n", 1234567.8);
    printf("%g should be 123.456\n", 123.456);
    printf("%g should be 1e+06\n", 1000000.0);
    printf("%g should be 10\n", 10.0);
    printf("%g should be 0.02\n", 0.02);
    printf("\n");

    /* Testing significant digits */
    printf("%g should be 123.457\n", 123.456789);
    printf("%g should be 0.000123456\n", 0.000123456);
    printf("\n");

    /* From David Gay's paper */
    printf("%.6f should be 1.234565\n", 1.234565);
    printf("%.6e should be 1.234565e+20\n", 1.234565e+20);
    printf("%.6e should be 1.234565e-20\n", 1.234565e-20);
    printf("\n");

    /* From Paxson's paper */
    dx.f = 12676506.0;
    dx.i.exp -= 102;
    printf("%.17g should be 2.4999999995498969e-24\n", dx.f);
    dx.f = 2.4999999995498969e-24;
    printf("%.17g should be 2.4999999995498969e-24\n", dx.f);
    dx.f = 6.567258882077402;
    printf("%.17g should be 6.567258882077402\n", dx.f);
    dx.i.exp += 952;
    printf("%.17g should be 2.5e+287\n", dx.f);
    dx.i.exp -= 2;
    printf("%.17g should be 6.25e+286\n", dx.f);

    return 0;
}
