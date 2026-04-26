#include <zephyr/kernel.h>
#include <zephyr/pm/pm.h>

int main(void)
{
    pm_state_force(0u, &(struct pm_state_info){
        PM_STATE_SOFT_OFF, 0, 0
    });
    k_sleep(K_FOREVER);
    return 0;
}
