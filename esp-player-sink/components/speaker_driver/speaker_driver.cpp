#include <lwip/netdb.h>
#include <esp_log.h>
#include "lwip/sockets.h"
#include "opus.h"
#include "driver/i2s.h"
#include "speaker_driver.hpp"
#define TAG "SPEAKER_TASK"
#define SPEAKER_PORT 1028
#define KEEPALIVE_IDLE 7200
#define KEEPALIVE_INTERVAL 75
#define KEEPALIVE_COUNT 10
#define CONFIG_EXAMPLE_IPV4 1

#define RATE 48000
#define BITS 16
#define FRAMELEN 2.5
#define CHANNELS 2
#define frame_size (RATE/1000*20)
void speaker_task(void *pvParameters);
static void do_decode(const int sock);
void i2s_config_proc();
void speaker_task(void *pvParameters) {
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *) &dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(SPEAKER_PORT);
        ip_protocol = IPPROTO_IP;
    }


    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %s", strerror(errno));
        vTaskDelete(NULL);
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    ESP_LOGI(TAG, "Socket created");

    int err =
        bind(listen_sock, (struct sockaddr *) &dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %s", strerror(errno));
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", SPEAKER_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %s", strerror(errno));
        goto CLEAN_UP;
    }

    i2s_config_proc();

    ESP_LOGI(TAG, "i2s config end.\n");

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage
            source_addr;  // Large enough for both IPv4 or IPv6
        socklen_t addr_len = sizeof(source_addr);
        int sock =
            accept(listen_sock, (struct sockaddr *) &source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %s", strerror(errno));
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
            inet_ntoa_r(((struct sockaddr_in *) &source_addr)->sin_addr, addr_str,
                sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

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

    OpusDecoder *decoder = opus_decoder_create(RATE, CHANNELS, &err);
    if (err < 0) {
        fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(err));
    }
    struct timeval start, end, start1, end1, start_total, end_total;
    opus_int16 out1[1920];
    int decodeSamples;

    char confirm[10] = "ok";
    //xTaskCreate(test_task, "test_task", 5000, NULL, 5, NULL);


    gettimeofday(&start1, NULL);
    len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
    gettimeofday(&end1, NULL);

    // printf("   len=%d        recv %ld us\n", len, end1.tv_usec - start1.tv_usec);
    if (len < 0) {
        ESP_LOGE(TAG, "Error occurred during receiving: errno %s", strerror(errno));
    }
    else if (len == 0) {
        ESP_LOGW(TAG, "Connection closed");
    }
    else {
        gettimeofday(&start1, NULL);
        send(sock, confirm, 10, 0);
        gettimeofday(&end1, NULL);
        printf("send is %ld us\n", end1.tv_usec - start1.tv_usec);
    }
    gettimeofday(&start_total, NULL);
    int i = 1;
    while (1) {
        ESP_LOGE("HEAP", "activate free_heap_size = %lu\n", esp_get_free_heap_size());
        ESP_LOGW(TAG, "i = %d\n", i++);
        // gettimeofday(&start, NULL);

        // gettimeofday(&start1, NULL);
        decodeSamples =
            opus_decode(decoder, rx_buffer, len, out1, frame_size, 0);
        // gettimeofday(&end1, NULL);
        // printf("opus_decode %ld us\n", end1.tv_usec - start1.tv_usec);                                                                                                                   
        // printf("decodeSamples= %d\n", decodeSamples);
        //-----------------------------------------------------------------//
        size_t BytesWritten;
        // gettimeofday(&start1, NULL);
        ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, out1, decodeSamples * 2, &BytesWritten, portMAX_DELAY));
        // gettimeofday(&end1, NULL);
        // printf("i2s_write %ld us\n", end1.tv_usec - start1.tv_usec);
        //-------------------------------------------------------------------//
        // gettimeofday(&start1, NULL);
        opus_int16 *out2 = out1;
        // gettimeofday(&start1, NULL);
        out2 = out2 + (decodeSamples);
        ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, out2, decodeSamples * 2, &BytesWritten, portMAX_DELAY));
        // gettimeofday(&end1, NULL);
        // printf("i2s_write %ld us\n", end1.tv_usec - start1.tv_usec);
        // vTaskDelay(5 / portTICK_PERIOD_MS);

        // printf("BytesWritten=%d\n", BytesWritten);
        // gettimeofday(&end, NULL);
        // printf("one frame %ld us\n", end.tv_usec - start.tv_usec);

        len = recv(sock, rx_buffer, sizeof(rx_buffer), 0);
        if (len > 0) {
            // gettimeofday(&end1, NULL);
            // printf("   len=%d        recv %ld us\n", len, end1.tv_usec - start1.tv_usec);0
            // gettimeofday(&start1, NULL);
            send(sock, confirm, 10, 0);
            // gettimeofday(&end1, NULL);
            // printf("send is %ld us\n", end1.tv_usec - start1.tv_usec);
        }
        if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
            break;
        }
        else if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %s", strerror(errno));
            break;
        }
        //------------------------------------------------------------//
        //-----------------------------------------------------------//
       


    }
    opus_decoder_destroy(decoder);
    gettimeofday(&end_total, NULL);
    printf("one---------- time %lf ms\n", (end_total.tv_sec - start_total.tv_sec) * 1000.0 + (end_total.tv_usec - start_total.tv_usec) / 1000.0);

}

void i2s_config_proc() {
    // i2s config for writing both channels of I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 3,
        .dma_buf_len = 1024,
        .use_apll = 1,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 12288000 };

    // i2s pinout
    static const i2s_pin_config_t pin_config = {
        .bck_io_num = 19,
        .ws_io_num = 1,
        .data_out_num = 18,
        .data_in_num = I2S_PIN_NO_CHANGE };


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
