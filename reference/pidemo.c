#include <stdio.h>

double denom = 1.0, PI4 = 1.0, sign = 1.0;
int n;

int main()
{
    for (n = 0; n < 100000; n++) {
        denom = denom + 2.0;
        sign *= -1.0;
        PI4 = PI4 + sign/denom;
        printf("N=%d, PI=%.17f\n", n, PI4*4);
    }

    return 0;
}
