/* Multiply 3x3 double matrices */
#include <stdio.h>

typedef double mat3x3[3][3];

static mat3x3 I = {
    { -1.0, 0.0, 0.0 },
    { 0.0, -1.0, 0.0 },
    { 0.0, 0.0, -1.0 }
};

static mat3x3 X = {
    { 1.0, 2.0, 3.0 },
    { 4.0, 5.0, 6.0 },
    { 7.0, 8.0, 9.0 }
};

static mat3x3 Y = {
    { -1.0, -1.0, -1.0 },
    { -1.0, -1.0, -1.0 },
    { -1.0, -1.0, -1.0 }
};

static void matmul(p, a, b)
mat3x3 p, a, b;
{
    int i, j, k;

    i = 0;
    while (i < 3) {
        j = 0;
        while (j < 3) {
            k = 0;
            p[i][j] = 0.0;
            while (k < 3) {
                p[i][j] += a[i][k]*b[k][j];
                ++k;
            }
            ++j;
        }
        ++i;
    }
}

int main()
{
    int i, j;

    matmul(Y, I, X);

    i = 0;
    while (i < 3) {
        j = 0;
        while (j < 3) {
            if (j) {
                printf(" ");
            }
            printf("%.1f", Y[i][j]);
            ++j;
        }
        printf("\n");
        ++i;
    }

    return 0;
}
