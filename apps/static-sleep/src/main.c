#include <zephyr/sys/poweroff.h>

int main(void)
{
    /* Deep sleep with no wake source: stays down until an external reset. */
    sys_poweroff();
    return 0;
}
