/* Fibonacci test */
#include <stdio.h>

int fib(n)
register int n;
{
    if (n < 2) {
        return n;
    }

    return fib(--n) + fib(--n);
}

int main()
{
    int n;

    printf("Fibonacci 33\n");

    n = fib(33);

    printf("%d\n", n);

    return 0;
}
