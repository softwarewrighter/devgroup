/* Eratosthenes Sieve Prime Number Program in C */
/* Byte Magazine, January 1983, "Eratosthenes Revisited" */
#include <stdio.h>

char flags[8191];
int main()
{
    int prime, k, count, iter;
    register int i;

    printf("1000 iterations\n");

    for (iter = 1; iter <= 1000; iter++) {  /* do program 1000 times */
        count = 0;                          /* prime counter */
        for (i = 0; i <= 8190; i++)         /* set all flags true */
            flags[i] = 1;
        for (i = 0; i <= 8190; i++)
            if (flags[i]) {                 /* found a prime */
                prime = i + i + 3;          /* twice index + 3 */
                for (k = i + prime; k <= 8190; k += prime)
                    flags[k] = 0;           /* kill all multiples */
                count++;                    /* primes found */
            }
    }

    printf("%d primes.\n", count);

    return 0;
}
