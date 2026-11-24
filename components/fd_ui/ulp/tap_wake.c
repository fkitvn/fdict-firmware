/* Dem hai lan cham man hinh luc CPU ngu sau.
 * GPIO17 = BOARD_TOUCH_INT. FT6336U keo INT xuong suot luc ngon tay con cham.
 * Mot lan cham qua lau, hoac khong co lan hai trong 450 ms, thi bo qua. */
#include "ulp_riscv_gpio.h"
#include "ulp_riscv_utils.h"

#define TAP_GPIO GPIO_NUM_17
#define TAP_HOLD_MIN_US 20000u
#define TAP_HOLD_MAX_US 400000u
#define TAP_GAP_MAX_US 450000u
#define TAP_POLL_US 5000u

static int press_then_release(void)
{
    ulp_riscv_delay_us(TAP_HOLD_MIN_US);
    if (ulp_riscv_gpio_get_level(TAP_GPIO) != 0) return 0;
    for (uint32_t t = TAP_HOLD_MIN_US; t < TAP_HOLD_MAX_US; t += TAP_POLL_US) {
        if (ulp_riscv_gpio_get_level(TAP_GPIO) != 0) return 1;
        ulp_riscv_delay_us(TAP_POLL_US);
    }
    return 0;
}

int main(void)
{
    for (;;) {
        while (ulp_riscv_gpio_get_level(TAP_GPIO) != 0) ulp_riscv_delay_us(TAP_POLL_US);
        if (!press_then_release()) {
            while (ulp_riscv_gpio_get_level(TAP_GPIO) == 0) ulp_riscv_delay_us(TAP_POLL_US);
            continue;
        }
        int second = 0;
        for (uint32_t t = 0; t < TAP_GAP_MAX_US; t += TAP_POLL_US) {
            if (ulp_riscv_gpio_get_level(TAP_GPIO) == 0) {
                ulp_riscv_delay_us(TAP_HOLD_MIN_US);
                if (ulp_riscv_gpio_get_level(TAP_GPIO) == 0) {
                    second = 1;
                    break;
                }
            }
            ulp_riscv_delay_us(TAP_POLL_US);
        }
        if (!second) continue;
        ulp_riscv_wakeup_main_processor();
        for (;;) ulp_riscv_delay_us(100000);
    }
}
