#ifndef IOT_WIFI_H
#define IOT_WIFI_H       

void wifi_init(void);
void wifi_scan(void);
int wifi_connect(const char *ssid, const char *pswd);
void wait_for_ip_addr(void);
int wifi_disconnect(void);

#endif /* IOT_WIFI_H */
