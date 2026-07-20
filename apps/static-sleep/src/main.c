#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>

int main(void)
{
    /* TEMPORARY validation: confirm the firmware boots and reaches the
     * soft-off call. Remove before measuring I_sleep (git checkout this file). */
    for (int i = 60; i > 0; i--) {
        printk("static-sleep: alive, forcing PM_STATE_SOFT_OFF in %d s\n", i);
        k_sleep(K_SECONDS(1));
    }
    printk("static-sleep: entering PM_STATE_SOFT_OFF now\n");

    pm_state_force(0u, &(struct pm_state_info){
        PM_STATE_SOFT_OFF, 0, 0
    });
    k_sleep(K_FOREVER);
    return 0;
}
