#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/*
 * Public-repo default profile:
 * 1. Keep USART1 enabled so the board can be provisioned and debugged over serial.
 * 2. Keep FreeRTOS enabled to showcase the task-based architecture by default.
 * 3. Disable Wi-Fi auto-connect to avoid committing real credentials to GitHub.
 */
#define APP_USART1_ENABLE                1U
#define APP_FREERTOS_ENABLE              1U

/* UART configuration */
#define APP_DEBUG_BAUD_RATE              115200U
#define APP_ESP_BAUD_RATE                115200U
#define APP_VERBOSE_BOOT_LOG             0U

/* Wi-Fi provisioning */
#define APP_WIFI_AUTO_CONNECT_ENABLE     1U
#define APP_WIFI_AUTO_SSID               "et1"
#define APP_WIFI_AUTO_PASSWORD           "12345678"
#define APP_WIFI_AUTO_START_DELAY_MS     1500U

#if ((APP_WIFI_AUTO_CONNECT_ENABLE == 0U) && (APP_USART1_ENABLE == 0U))
#error "Enable APP_USART1_ENABLE or APP_WIFI_AUTO_CONNECT_ENABLE so the device still has a Wi-Fi provisioning path."
#endif

#endif
