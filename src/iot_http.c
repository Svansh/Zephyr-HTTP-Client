#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/tls_credentials.h>
#include "ca_certificate.h"
#include "iot_http.h"

#define MAX_RECV_BUF_LEN 512

static uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];

static int setup_socket(net_sa_family_t sa_family, const char *server, int port, int *sock, struct net_sockaddr *addr, net_socklen_t addr_len) {

    int ret = 0;
    const char *family_str = (sa_family == NET_AF_INET) ? "IPv4" : "IPv6";

    memset(addr, 0, addr_len);

    struct zsock_addrinfo hints = {0};
    struct zsock_addrinfo *res = NULL;
    char port_str[6];

    hints.ai_family = sa_family;
    hints.ai_socktype = NET_SOCK_STREAM;
    sprintf(port_str, "%d", port);

    ret = zsock_getaddrinfo(server, port_str, &hints, &res);
    if(ret < 0 || res == NULL) {
        printk("Cannot resolve %s address (%d)\n", family_str, -errno);
        return -EHOSTUNREACH;
    }

    memcpy(addr, res->ai_addr, MIN(res->ai_addrlen, addr_len));
    zsock_freeaddrinfo(res);

    if(IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
        sec_tag_t sec_tag_list[] = { CA_CERTIFICATE_TAG };
        *sock = zsock_socket(sa_family, NET_SOCK_STREAM, NET_IPPROTO_TLS_1_2);
        if(*sock >= 0) {
            ret = zsock_setsockopt(*sock, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, sec_tag_list, sizeof(sec_tag_list));
            if(ret < 0) {
                printk("Failed to set %s secure option (%d)\n", family_str, -errno);
                ret = -errno;
            }
            ret = zsock_setsockopt(*sock, ZSOCK_SOL_TLS, ZSOCK_TLS_HOSTNAME, HOSTNAME, sizeof(TLS_PEER_HOSTNAME));
            if(ret < 0) {
                printk("Failed to set %s TLS_HOSTNAME option (%d)\n", family_str, -errno);
                ret = -errno;
            }
        }
    } else {
        *sock = zsock_socket(sa_family, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    }

    if(*sock < 0) {
        printk("Failed to create %s HTTP socket (%d)\n", family_str, -errno);
        return ret;
    }

    return 0;
}

static int connect_socket(net_sa_family_t sa_family, const char *server, int port, int *sock, struct net_sockaddr *addr, net_socklen_t addr_len) {

    int ret = 0;

    ret = setup_socket(sa_family, server, port, sock, addr, addr_len);
    if(ret < 0 || *sock < 0) {
        return -1;
    }

    ret = zsock_connect(*sock, addr, addr_len);
    if(ret < 0) {
        printk("Cannot connect to %s remote (%d)\n", (sa_family == NET_AF_INET) ? "IPv4" : "IPv6", -errno);
        zsock_close(*sock);
        *sock = -1;
        ret = -errno;
    }

    return ret;
}

static int response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data) {
    
    if(final_data == HTTP_DATA_MORE) {
        printk("Partial data received (%zu bytes)\n", rsp->data_len);
    } else if(final_data == HTTP_DATA_FINAL) {
        printk("All the data received (%zu bytes)\n", rsp->data_len);
    }

    // printk("Response status %s\n", rsp->http_status);
    // printk("Response data: %.*s\n", (int)rsp->data_len, rsp->recv_buf);

    return 0;
}

void init_http_request(void) {

    struct net_sockaddr_in addrv4 = {0};
    struct net_sockaddr_in6 addrv6 = {0};
    int sock4 = -1, sock6 = -1;
    int ret = 0;
    int32_t timeout = 3 * MSEC_PER_SEC;
    int port = HTTPS_PORT;
    struct http_request req;

    if(IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {

        ret = tls_credential_add(CA_CERTIFICATE_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca_certificate, sizeof(ca_certificate));

        if(ret < 0) {
            printk("Failed to register public certificate: %d\n", ret);
            return;
        }

        port = HTTPS_PORT;
    }

    if(IS_ENABLED(CONFIG_NET_IPV4)) {

        
        connect_socket(NET_AF_INET, HOSTNAME, port, &sock4, (struct net_sockaddr *)&addrv4, sizeof(addrv4));
        if(sock4 < 0) {
            printk("Cannot create HTTP IPv4 connection.\n");
            return;
        }

        char ip[NET_IPV4_ADDR_LEN];
        zsock_inet_ntop(AF_INET, &addrv4.sin_addr, ip, sizeof(ip));
        printk("Connecting to %s:%d\n", ip, ntohs(addrv4.sin_port));
        
        memset(&req, 0, sizeof(req));

        req.method = HTTP_GET;
        req.url = "/";
		req.host = HOSTNAME;
		req.protocol = "HTTP/1.1";
		req.response = response_cb;
		req.recv_buf = recv_buf_ipv4;
		req.recv_buf_len = sizeof(recv_buf_ipv4);

        ret = http_client_req(sock4, &req, timeout, NULL);
        if(ret < 0) {
            printk("Client error %d\n", ret);
        }

        zsock_close(sock4);

    }

    sock4 = -1;

}