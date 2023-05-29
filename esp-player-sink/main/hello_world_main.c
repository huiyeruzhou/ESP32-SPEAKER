/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "opus.h"

#include <lwip/netdb.h>
#include "lwip/sockets.h"

#include "driver/i2s.h"
#include <sys/time.h>
#define ESP_WIFI_SSID "dududud"
#define ESP_WIFI_PASS "00000000"
#define ESP_MAXIMUM_RETRY 5
#define TAG "luo980"

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define PORT 1028
#define KEEPALIVE_IDLE 7200
#define KEEPALIVE_INTERVAL 75
#define KEEPALIVE_COUNT 10
#define CONFIG_EXAMPLE_IPV4 1

#define RATE 48000
#define BITS 16
#define FRAMELEN 2.5
#define CHANNELS 2
#define frame_size (RATE/1000*20)
static int s_retry_num = 0;

void wifi_init_sta(void);
static void event_handler(void* arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void* event_data);
static void tcp_server_task(void* pvParameters);
static void do_retransmit(const int sock);
static void do_decode(const int sock);
void i2s_config_proc();

/*
void esp_print_tasks(void);
void test_task(void *param);

void esp_print_tasks(void)
{
    char *pbuffer = (char *)calloc(1, 2048);
    printf("---------------------------------------------\r\n");
    vTaskGetRunTimeStats(pbuffer);
    printf("%s", pbuffer);
    printf("----------------------------------------------\r\n");
    free(pbuffer);
}

void test_task(void *param)
{
    while(1) {
        esp_print_tasks();
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

*/

void app_main(void) {
    printf("Hello world!\n");
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // wifi_connect();

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ", CONFIG_IDF_TARGET,
           chip_info.cores, (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
        return;
    }

    wifi_init_sta();

    printf("%uMB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                         : "external");

    printf("Minimum free heap size: %d bytes\n",
           esp_get_minimum_free_heap_size());

#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(tcp_server_task, "tcp_server", 18000, (void*)AF_INET, 3, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(tcp_server_task, "tcp_server", 18000, (void*)AF_INET6, 3, NULL);
#endif

    for (int i = 100; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = ESP_WIFI_SSID,
                .password = ESP_WIFI_PASS,
                /* Authmode threshold resets to WPA2 as default if password
                 * matches WPA2 standards (pasword len => 8). If you want to
                 * connect the device to deprecated WEP/WPA networks, Please set
                 * the threshold value to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and
                 * set the password with length and format matching to
                 * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
                 */
                .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("TAG", "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT)
     * or connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
     * The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we
     * can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", ESP_WIFI_SSID,
                 ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 ESP_WIFI_SSID, ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static void event_handler(void* arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void do_retransmit(const int sock) {
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            rx_buffer[len] = 0;  // Null-terminate whatever is received and
                                 // treat it like a string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

            // send() can return less bytes than supplied length.
            // Walk-around for robust implementation.
            int to_write = len;
            while (to_write > 0) {
                int written =
                    send(sock, rx_buffer + (len - to_write), to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d",
                             errno);
                    // Failed to retransmit, giving up
                    return;
                }
                to_write -= written;
            }
        }
    } while (len > 0);
}

static void tcp_server_task(void* pvParameters) {
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in* dest_addr_ip4 = (struct sockaddr_in*)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }
#ifdef CONFIG_EXAMPLE_IPV6
    else if (addr_family == AF_INET6) {
        struct sockaddr_in6* dest_addr_ip6 = (struct sockaddr_in6*)&dest_addr;
        bzero(&dest_addr_ip6->sin6_addr.un,
              sizeof(dest_addr_ip6->sin6_addr.un));
        dest_addr_ip6->sin6_family = AF_INET6;
        dest_addr_ip6->sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }
#endif

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
    // Note that by default IPV6 binds to both protocols, it is must be disabled
    // if both protocols used at the same time (used in CI)
    setsockopt(listen_sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif

    ESP_LOGI(TAG, "Socket created");

    int err =
        bind(listen_sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage
            source_addr;  // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock =
            accept(listen_sock, (struct sockaddr*)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option

        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval,
                   sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in*)&source_addr)->sin_addr, addr_str,
                        sizeof(addr_str) - 1);
        }
#ifdef CONFIG_EXAMPLE_IPV6
        else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6*)&source_addr)->sin6_addr,
                         addr_str, sizeof(addr_str) - 1);
        }
#endif
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        i2s_config_proc();

        ESP_LOGI(TAG, "i2s config end.\n");

        do_decode(sock);

        ESP_LOGI(TAG, "Exit Decoding.\n");
        // do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

static void do_decode(const int sock) {
    int err, len;
    unsigned char rx_buffer[640];

    OpusDecoder* decoder = opus_decoder_create(RATE, CHANNELS, &err);
    if (err < 0) {
        fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(err));
    }
    struct timeval start, end,start1,end1,start2,end2,start_total,end_total;
    opus_int16 out1[1920];
    int decodeSamples;
	int a=0;
    char confirm[10]="ok";
    //xTaskCreate(test_task, "test_task", 5000, NULL, 5, NULL);


        gettimeofday(&start1,NULL);
        len = recv(sock,rx_buffer, sizeof(rx_buffer), 0);
        gettimeofday(&end1,NULL);
        
		printf("   len=%d        recv %dus\n",len,end1.tv_usec-start1.tv_usec);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        }
        else{
            gettimeofday(&start1,NULL);
            send(sock,confirm,10,0);    
            gettimeofday(&end1,NULL);
            printf("send is %dus\n",end1.tv_usec-start1.tv_usec);
        }
        gettimeofday(&start_total,NULL);
    while (1) {
        gettimeofday(&start,NULL);
		
        
			
            //rx_buffer[len] = 0;  // Null-terminate whatever is received and
                                 // treat it like a string
            //ESP_LOGI(TAG, "Received %d", len);
			/*
			gettimeofday(&start2,NULL);
            a+=1;
			printf("a=%d\n",a);
			if(a==2){
				
				send(sock,confirm,2,0);
				gettimeofday(&end1,NULL);
				printf("send %dus\n",end1.tv_usec-start1.tv_usec);
				if (len>0){
					a=0;
				}
				else{
					send(sock,confirm,2,0);	
				}
			}
			gettimeofday(&end2,NULL);
			printf("if is %dus\n",end2.tv_usec-start1.tv_usec);
			*/
            //gettimeofday(&start1,NULL);
            //memset(out1,0,sizeof(out1));
            //gettimeofday(&end1,NULL);
            //ESP_LOGI(TAG,"memset %dus",end1.tv_usec-start1.tv_usec);
            //ESP_LOGI(TAG,"end2 %dus",end2.tv_usec-start2.tv_usec);
    //--------------------------------------------------------------------//
        gettimeofday(&start1,NULL);
        decodeSamples =
        opus_decode(decoder, rx_buffer, len, out1, frame_size, 0);
        gettimeofday(&end1,NULL);
		printf("opus_decode %dus\n",end1.tv_usec-start1.tv_usec);
        printf("decodeSamples= %d\n", decodeSamples);
//-----------------------------------------------------------------//
        size_t BytesWritten;
        gettimeofday(&start1,NULL);
        ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, out1, decodeSamples*2, &BytesWritten, portMAX_DELAY));
        gettimeofday(&end1,NULL);
        printf("i2s_write %dus\n",end1.tv_usec-start1.tv_usec);
//-------------------------------------------------------------------//
        gettimeofday(&start1,NULL);
        opus_int16 * out2=out1;
        len = recv(sock,rx_buffer, sizeof(rx_buffer), 0);
        if (len>0){
            gettimeofday(&end1,NULL);
		    printf("   len=%d        recv %dus\n",len,end1.tv_usec-start1.tv_usec);
            gettimeofday(&start1,NULL);
            send(sock,confirm,10,0);    
            gettimeofday(&end1,NULL);
            printf("send is %dus\n",end1.tv_usec-start1.tv_usec);
        }
        if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            break;
        }
        else if(len <0){
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
            break;
        }
//------------------------------------------------------------//
//-----------------------------------------------------------//

        gettimeofday(&start1,NULL);
        out2=out2+(decodeSamples);
        ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, out2, decodeSamples*2, &BytesWritten, portMAX_DELAY));
        gettimeofday(&end1,NULL);
        printf("i2s_write %dus\n",end1.tv_usec-start1.tv_usec);
        vTaskDelay(5 / portTICK_PERIOD_MS);

        /*
        gettimeofday(&start1,NULL);
        out2=out2+(decodeSamples/2);
        ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, out2, decodeSamples*2, &BytesWritten, portMAX_DELAY));
        gettimeofday(&end1,NULL);
        printf("i2s_write %dus\n",end1.tv_usec-start1.tv_usec);
        */
        printf("BytesWritten=%d\n",BytesWritten);
        gettimeofday(&end,NULL);
		printf("one frame %dus\n",end.tv_usec-start.tv_usec);
    }
    gettimeofday(&end_total,NULL);
    printf("one---------- time %lf ms\n",(end_total.tv_sec-start_total.tv_sec)*1000.0+(end_total.tv_usec-start_total.tv_usec)/1000.0);

}

void i2s_config_proc() {
    // i2s config for writing both channels of I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 3,
        .dma_buf_len = 1024,
        .use_apll = 1,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 12288000};

    // i2s pinout
    static const i2s_pin_config_t pin_config = {
        .bck_io_num = 19,
        .ws_io_num = 1,
        .data_out_num = 18,
        .data_in_num = I2S_PIN_NO_CHANGE};


    // install and start i2s driver
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, 48000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    // enable the DAC channels
    // i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    // clear the DMA buffers
    i2s_zero_dma_buffer(I2S_NUM_0);

    i2s_start(I2S_NUM_0);
}
