#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

int main(void)
{
    /* TEMPORARY validation: confirm the firmware boots and reaches the
     * poweroff call. Remove before measuring I_sleep (git checkout this file). */
    for (int i = 60; i > 0; i--) {
        printk("static-sleep: alive, entering deep sleep in %d s\n", i);
        k_sleep(K_SECONDS(1));
    }
    printk("static-sleep: entering deep sleep now\n");

    sys_poweroff();
    return 0;
}
