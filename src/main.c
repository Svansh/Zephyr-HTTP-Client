#include <zephyr/kernel.h>
#include "iot_wifi.h"
#include "iot_http.h"

#define WIFI_SSID           CONFIG_APP_WIFI_SSID
#define WIFI_PSWD           CONFIG_APP_WIFI_PASSWORD

#define WIFI_MAX_RETRIES    5

int main()
{
    int ret;
    wifi_init();
    wifi_scan();

    for (int attempt = 1; attempt <= WIFI_MAX_RETRIES; attempt++) {
        printk("Wi-Fi connect attempt %d/%d...\n", attempt, WIFI_MAX_RETRIES);
        ret = wifi_connect(WIFI_SSID, WIFI_PSWD);
        if (ret == 0) {
            break;
        }
        printk("Connect attempt failed (%d), retrying...\n", ret);
        k_sleep(K_SECONDS(2));
    }

    wait_for_ip_addr();
    init_http_request();

    return 0;
}