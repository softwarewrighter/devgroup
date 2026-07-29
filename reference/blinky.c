/* LED follows pushbutton */
#include <ledswio.h>

int main()
{
    register char *ledsw = LEDSWDAT;

    while (1) {
        *ledsw = *ledsw;
    }

    return 0;
}
