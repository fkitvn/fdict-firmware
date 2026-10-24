/* Doc dien ap pin qua ADC1 oneshot + hieu chuan curve-fitting.
 * Phan cung FNK0104A/B: Vbat -> cau chia 1:2 -> GPIO9 (ADC1_CH8), theo Freenove Sketch_05.1_Battery_Voltage.
 * Timer esp_timer 180 s (khong chay trong task LVGL); UI doc fd_batt_get_pct() trong on_tick (da giu lock LVGL).
 * TP4054: chan CHRG khong noi ve MCU -> khong phat hien dang sac. */
#include "fd_batt.h"
#include "sdkconfig.h"

#if CONFIG_FD_BATTERY_ENABLE
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define TAG "fd_batt"
#define BATT_SAMPLES 16
#define BATT_PERIOD_US (180LL * 1000 * 1000)

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_ch;
static esp_timer_handle_t s_timer;
static atomic_int s_mv = -1, s_pct = -1;
static int s_mv_ema = -1;

/* Duong cong xa Li-ion 1 cell (mV -> %) */
static const struct { int mv, pct; } CURVE[] = {
    {4200, 100}, {4100, 90}, {4000, 79}, {3900, 66}, {3800, 52}, {3750, 42},
    {3700, 30}, {3650, 20}, {3600, 12}, {3500, 5}, {3300, 0},
};

static int mv_to_pct(int mv)
{
    if (mv >= CURVE[0].mv) return 100;
    for (size_t i = 1; i < sizeof(CURVE) / sizeof(CURVE[0]); i++) {
        if (mv >= CURVE[i].mv) {
            int dm = CURVE[i - 1].mv - CURVE[i].mv, dp = CURVE[i - 1].pct - CURVE[i].pct;
            return CURVE[i].pct + (mv - CURVE[i].mv) * dp / dm;
        }
    }
    return 0;
}

static void batt_read(void *arg)
{
    (void)arg;
    int sum = 0, n = 0;
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int mv;
        if (adc_oneshot_get_calibrated_result(s_adc, s_cali, s_ch, &mv) == ESP_OK) {
            sum += mv;
            n++;
        }
    }
    if (n == 0) {
        ESP_LOGW(TAG, "batt: doc ADC loi");
        return;
    }
    int mv = sum / n * CONFIG_FD_BATTERY_DIVIDER / 100;
    s_mv_ema = (s_mv_ema < 0) ? mv : (s_mv_ema * 3 + mv) / 4;   /* EMA alpha 1/4 */
    int pct = mv_to_pct(s_mv_ema), old = atomic_load(&s_pct);
    /* tre +-2%: chi cap nhat khi lech >= 2, hoac cham 0/100 */
    if (old < 0 || pct - old >= 2 || old - pct >= 2 || pct == 0 || pct == 100) {
        atomic_store(&s_pct, pct);
    }
    atomic_store(&s_mv, s_mv_ema);
    ESP_LOGI(TAG, "batt: %d mV %d%%", s_mv_ema, atomic_load(&s_pct));
}

esp_err_t fd_batt_init(void)
{
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(CONFIG_FD_BATTERY_ADC_GPIO, &unit, &s_ch);
    if (err != ESP_OK || unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d khong phai ADC1", CONFIG_FD_BATTERY_ADC_GPIO);
        return ESP_ERR_INVALID_ARG;
    }
    const adc_oneshot_unit_init_cfg_t ucfg = {.unit_id = ADC_UNIT_1, .clk_src = 0, .ulp_mode = ADC_ULP_MODE_DISABLE};
    if ((err = adc_oneshot_new_unit(&ucfg, &s_adc)) != ESP_OK) return err;
    const adc_oneshot_chan_cfg_t ccfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if ((err = adc_oneshot_config_channel(s_adc, s_ch, &ccfg)) != ESP_OK) return err;
    const adc_cali_curve_fitting_config_t kcfg = {
        .unit_id = ADC_UNIT_1, .chan = s_ch, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if ((err = adc_cali_create_scheme_curve_fitting(&kcfg, &s_cali)) != ESP_OK) {
        ESP_LOGE(TAG, "cali: %s", esp_err_to_name(err));
        return err;
    }
    batt_read(NULL);
    const esp_timer_create_args_t targs = {.callback = batt_read, .arg = NULL, .dispatch_method = ESP_TIMER_TASK,
                                           .name = "batt", .skip_unhandled_events = true};
    if ((err = esp_timer_create(&targs, &s_timer)) != ESP_OK) return err;
    ESP_LOGI(TAG, "GPIO%d (ADC1_CH%d), chia x%d/100, chu ky 180 s", CONFIG_FD_BATTERY_ADC_GPIO, (int)s_ch,
             CONFIG_FD_BATTERY_DIVIDER);
    return esp_timer_start_periodic(s_timer, BATT_PERIOD_US);
}

int fd_batt_get_pct(void) { return atomic_load(&s_pct); }
int fd_batt_get_mv(void) { return atomic_load(&s_mv); }

#else
esp_err_t fd_batt_init(void) { return ESP_OK; }
int fd_batt_get_pct(void) { return -1; }
int fd_batt_get_mv(void) { return -1; }
#endif