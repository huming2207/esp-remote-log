#include <stdio.h>
#include <string.h>

#include <esp_system.h>
#include <esp_log.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>


#include "remote_log.h"

#define TAG "remote_log"
#define LOG_PORT 23

static int log_serv_sockfd = -1;
static int log_sockfd = -1;
static struct sockaddr_in log_serv_addr, log_cli_addr;
static char fmt_buf[LOG_FORMAT_BUF_LEN];
static vprintf_like_t orig_vprintf_cb;

/**
 * Code originally from here, modified for TCP server:
 *  https://github.com/MalteJ/embedded-esp32-component-udp_logging/blob/master/udp_logging.c
 * @param str
 * @param list
 * @return
 */
static int remote_log_vprintf_cb(const char *str, va_list list)
{

    int ret = 0, len = 0;
    char task_name[16];

    // Can't really understand what the hell is this...
    char *cur_task = pcTaskGetTaskName(xTaskGetCurrentTaskHandle());
    strncpy(task_name, cur_task, 16);
    task_name[15] = 0;

    // Why need to compare the task name anyway??
    if (strncmp(task_name, "tiT", 16) != 0) {

        len = vsprintf((char*)fmt_buf, str, list);

        // Send off the formatted payload
        if((ret = send(log_sockfd, fmt_buf, len, 0)) < 0) {
            ESP_LOGE(TAG, "Oops, failed to send the message to clients, send returns %d, %s",
                    ret, strerror(errno));
            remote_log_free();
        }
    }

    return vprintf(str, list);
}

int remote_log_init()
{
    int ret = 0;

    memset(&log_serv_addr, 0, sizeof(log_serv_addr));
    memset(&log_cli_addr, 0, sizeof(log_cli_addr));

    log_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    log_serv_addr.sin_family = AF_INET;
    log_serv_addr.sin_port = htons(LOG_PORT);

    if((log_serv_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
        ESP_LOGE(TAG, "Failed to create socket, fd value: %d", log_serv_sockfd);
        return log_serv_sockfd;
    }

    ESP_LOGI(TAG, "Socket FD is %d", log_serv_sockfd);

    int reuse_option = 1;
    if((ret = setsockopt(log_serv_sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_option, sizeof(reuse_option))) < 0) {
        ESP_LOGE(TAG, "Failed to set reuse, returned %d, %s", ret, strerror(errno));
        remote_log_free();
        return ret;
    }

    if((ret = bind(log_serv_sockfd, (struct sockaddr *)&log_serv_addr, sizeof(log_serv_addr))) < 0) {
        ESP_LOGE(TAG, "Failed to bind the port, maybe someone is using it?? Reason: %d, %s", ret, strerror(errno));
        remote_log_free();
        return ret;
    }

    if((ret = listen(log_serv_sockfd, 1)) != 0) {
        ESP_LOGE(TAG, "Failed to listen, returned: %d", ret);
        return ret;
    }

    // Set timeout
    struct timeval timeout = {
            .tv_sec = 30,
            .tv_usec = 0
    };

    // Set receive timeout
    if ((ret = setsockopt(log_serv_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout))) < 0) {
        ESP_LOGE(TAG, "Setting receive timeout failed");
        remote_log_free();
        return ret;
    }

    // Set send timeout
    if ((ret = setsockopt(log_serv_sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout))) < 0) {
        ESP_LOGE(TAG, "Setting send timeout failed");
        remote_log_free();
        return ret;
    }

    ESP_LOGI(TAG, "Server created, please use telnet to debug me in 30 seconds!");

    size_t cli_addr_len = sizeof(log_cli_addr);
    if((log_sockfd = accept(log_serv_sockfd, (struct sockaddr *)&log_cli_addr, &cli_addr_len)) < 0) {
        ESP_LOGE(TAG, "Failed to accept, returned: %d, %s", ret, strerror(errno));
        remote_log_free();
        return ret;
    }

    // Bind vprintf callback
    orig_vprintf_cb = esp_log_set_vprintf(remote_log_vprintf_cb);

    ESP_LOGI(TAG, "Logger vprintf function bind successful!");

    return 0;
}

int remote_log_free()
{
    int ret = 0;
    if((ret = close(log_serv_sockfd)) != 0) {
        ESP_LOGE(TAG, "Cannot close the socket! Have you even open it?");
        return ret;
    }

    if(orig_vprintf_cb != NULL) {
        esp_log_set_vprintf(orig_vprintf_cb);
    }

    return ret;
}
