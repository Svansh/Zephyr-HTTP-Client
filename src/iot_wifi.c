#include <zephyr/logging/log.h>
LOG_LEVEL_SET(LOG_LEVEL_DBG);

#include <zephyr/kernel.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

BUILD_ASSERT(sizeof(CONFIG_APP_WIFI_SSID) > 1, "Set Wi-Fi SSID");

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback wifi_scan_cb;
static struct net_mgmt_event_callback ip_mgmt_cb;

K_SEM_DEFINE(wifi_mgmt_sem, 0, 1);
K_SEM_DEFINE(wifi_scan_sem, 0, 1);
K_SEM_DEFINE(ip_mgmt_sem, 0, 1);

void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface) {
    struct wifi_status *status = (struct wifi_status *)cb->info;
    if(mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        if (status->status == 0) {
            printk("Wi-Fi connected successfully\n");
            k_sem_give(&wifi_mgmt_sem);
        } else {
            printk("Wi-Fi connection failed with status: %d\n", status->status);
            // k_sem_give(&wifi_mgmt_sem); // Release semaphore even on failure to avoid deadlock
        }
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        if(status->status == 0) {
            printk("Wi-Fi disconnected successfully\n");
            k_sem_take(&wifi_mgmt_sem, K_NO_WAIT);
            k_sem_take(&ip_mgmt_sem, K_NO_WAIT);
        } else {
            printk("Wi-Fi disconnection failed with status: %d\n", status->status);
            k_sem_give(&wifi_mgmt_sem); // Release semaphore even on failure to avoid deadlock
        }
    }
}

static void on_wifi_scan_result(struct net_mgmt_event_callback *cb) {
    const struct wifi_scan_result *entry = (const struct wifi_scan_result *)cb->info;
    char ssid_str[WIFI_SSID_MAX_LEN + 1];

    memset(ssid_str, 0, sizeof(entry->ssid));
    memcpy(ssid_str, entry->ssid, entry->ssid_length);

    printk("  SSID: %-32s | Chan: %-3u | RSSI: %-4d | Band: %s | Security: %s\n",
           ssid_str,
           entry->channel,
           entry->rssi,
           wifi_band_txt(entry->band),
           wifi_security_txt(entry->security));
}

static void on_wifi_scan_done(struct net_mgmt_event_callback *cb) {
    const struct wifi_status *status = (const struct wifi_status *)cb->info;
    if (status->status) {
        printk("Scan failed (%d)\n", status->status);
    } else {
        printk("Scan complete.\n");
    }
    k_sem_give(&wifi_scan_sem);
}

void on_scan_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface) {
    switch (mgmt_event) {
        case NET_EVENT_WIFI_SCAN_RESULT:
            on_wifi_scan_result(cb);
            break;
        case NET_EVENT_WIFI_SCAN_DONE:
            on_wifi_scan_done(cb);
            break;
        default:
            break;
    }
}

void on_ip_acquired_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface) {
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        printk("IP address acquired event received\n");
        k_sem_give(&ip_mgmt_sem);
    }
}

void wifi_init(void) {
    // Initializing the network management event callback for Wi-Fi events
    net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_event_handler, NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_init_event_callback(&wifi_scan_cb, on_scan_handler, NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
    net_mgmt_init_event_callback(&ip_mgmt_cb, on_ip_acquired_handler, NET_EVENT_IPV4_ADDR_ADD);

    // Adding the Wi-Fi event callback to the network management system
    net_mgmt_add_event_callback(&wifi_mgmt_cb);
    net_mgmt_add_event_callback(&wifi_scan_cb);
    net_mgmt_add_event_callback(&ip_mgmt_cb);
}

void wifi_scan(void) {
    struct net_if *iface = net_if_get_default();
    int ret;

    printk("Scanning for Wi-Fi networks...\n");

    ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
    if (ret) {
        printk("Scan request failed (%d)\n", ret);
        return;
    }

    k_sem_take(&wifi_scan_sem, K_SECONDS(10)); // Wait for scan to complete
}

int wifi_connect(const char *ssid, const char *pswd) {
    int ret;
    struct wifi_connect_req_params params;
    struct net_if *iface = net_if_get_default();

    memset(&params, 0, sizeof(params));
    params.ssid = (const uint8_t *)ssid;
    params.ssid_length = strlen(ssid);
    params.psk = (const uint8_t *)pswd;
    params.psk_length = strlen(pswd);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;
    params.mfp = WIFI_MFP_OPTIONAL;

    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (ret) {
        printk("Wi-Fi connect request failed (%d)\n", ret);
        return ret;
    }

    // if(k_sem_take(&wifi_mgmt_sem, K_SECONDS(10)) != 0) {
    //     printk("Wi-Fi connection timed out\n");
    //     return -EAGAIN;
    // }

    return ret;
}

void wait_for_ip_addr(void) {
    int ret;
    struct wifi_iface_status status;
    struct net_if *iface = net_if_get_default();

    char ip_addr[NET_IPV4_ADDR_LEN];
    char gw_addr[NET_IPV4_ADDR_LEN];

    k_sem_take(&ip_mgmt_sem, K_FOREVER);

    memset(&ip_addr, 0, sizeof(ip_addr));
    if(net_addr_ntop(NET_AF_INET, &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr, ip_addr, sizeof(ip_addr)) == NULL) {
        printk("Error: Could not convert IP address to string\r\n");
    }

    memset(&gw_addr, 0, sizeof(gw_addr));
    if(net_addr_ntop(NET_AF_INET, &iface->config.ip.ipv4->gw, gw_addr, sizeof(gw_addr)) == NULL) {
        printk("Error: Could not convert gateway address to string\r\n");
    }

    ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
    if (ret) {
        printk("Failed to get Wi-Fi interface status (%d)\n", ret);
        return;
    }

    printk("WiFi status:\r\n");
    if (status.state >= WIFI_STATE_ASSOCIATED) {
        printk("  SSID: %-32s\r\n", status.ssid);
        printk("  Band: %s\r\n", wifi_band_txt(status.band));
        printk("  Channel: %d\r\n", status.channel);
        printk("  Security: %s\r\n", wifi_security_txt(status.security));
        printk("  IP address: %s\r\n", ip_addr);
        printk("  Gateway: %s\r\n", gw_addr);
    }   // Wait for the device to obtain an IP address
}

int wifi_disconnect(void) {
    struct net_if *iface = net_if_get_default();
    int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
    if (ret) {
        printk("Wi-Fi disconnect request failed (%d)\n", ret);
        return ret;
    }
    return 0;
}