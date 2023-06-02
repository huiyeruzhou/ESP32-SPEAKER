#include <cstdio>
// #include <string>
// #include <string.h>
// #include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "opus.h"
#include <lwip/netdb.h>
#include "lwip/sockets.h"
#include "driver/i2s.h"
#include <sys/time.h>
#include "mic_driver.hpp"

#define TAG "MIC_TASK"
#define RATE 16000
#define BITS 16
#define CHANNELS 2
#define frame_size (RATE/1000*20)
#define encodedatasize (frame_size*(BITS/8)*CHANNELS)
#define MAX_PACKET_SIZE (640)



#define KEEPALIVE_IDLE 7200
#define KEEPALIVE_INTERVAL 75
#define KEEPALIVE_COUNT 10
#define CONFIG_EXAMPLE_IPV4 1

void micronphone_task(void *pvParameters);
void do_decode(const int sock);
void i2s_config_proc();
OpusEncoder *encoder_init(opus_int32 sampling_rate,
    int channels,
    int application);
typedef struct OpusEncoder OpusEncoder;
void micronphone_task(void *pvParameters) {

    ESP_LOGI(TAG, "micronphone_task create success");

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
        dest_addr_ip4->sin_port = htons(MICROPHONE_PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err =
        bind(listen_sock, (struct sockaddr *) &dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", MICROPHONE_PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
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
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Set tcp keepalive option
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval,
            sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        /*
                 // 设置超时时间
                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 8000;
                if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
                    perror("setsockopt error");
                    return -1;
                }
                */
                // Convert ip address to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *) &source_addr)->sin_addr, addr_str,
                sizeof(addr_str) - 1);
        }

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        // do_decode(sock);
        int len;
        unsigned char cbits[500];
        unsigned char *tx_buffer = cbits;
        unsigned char rx_ok[10];
        OpusEncoder *enc = encoder_init(RATE, CHANNELS, OPUS_APPLICATION_VOIP);
        struct timeval start, end, start1, end1, start_total, end_total;
        opus_int16 *in1 = (opus_int16 *) malloc(sizeof(opus_int16) * encodedatasize);
        //char * i2s_rx = in1;
        //xTaskCreate(test_task, "test_task", 5000, NULL, 5, NULL);
        int counter = 1;
        int len_opus;
        gettimeofday(&start_total, NULL);
        while (1) {
            gettimeofday(&start, NULL);

            //--------------------------------------------------------------------//
            size_t BytesRead;
            gettimeofday(&start1, NULL);
            ESP_ERROR_CHECK(i2s_read(I2S_NUM_0, (char *) in1, encodedatasize, &BytesRead, portMAX_DELAY));
            gettimeofday(&end1, NULL);
            int left = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGE(TAG, "%d byte of stack left", left);
            printf("i2s_read %ld us,BytesRead=%d \n", end1.tv_usec - start1.tv_usec, BytesRead);
            //-----------------------------------------------------------------//
            gettimeofday(&start1, NULL);
            len_opus = opus_encode(enc, in1, frame_size, tx_buffer, MAX_PACKET_SIZE);
            gettimeofday(&end1, NULL);
            printf("opus_encode %ld us  ,len_opus[counter]= %d\n", end1.tv_usec - start1.tv_usec, len_opus);
            //-------------------------------------------------------------------//
            //
            if (len_opus < 0) {
                printf("failed to encode:%s \n", opus_strerror(len_opus));
                goto Done;
            }
            //*(int*)cbits_vtmp =tv;
            printf("recving \n");
            gettimeofday(&start1, NULL);
            len = recv(sock, rx_ok, sizeof(rx_ok), 0);//MSG_DONTWAIT);
            gettimeofday(&end1, NULL);
            printf("recv is %ld us\n", end1.tv_usec - start1.tv_usec);
            if (len == 0) {
                ESP_LOGW(TAG, "Connection closed");
                goto Done;
            }
            else if (len < 0) {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %s", strerror(errno));
                goto Done;
            }
            else {
                gettimeofday(&start1, NULL);
                int sendlen = send(sock, tx_buffer, len_opus, 0);
                gettimeofday(&end1, NULL);
                printf("send is %ld us\n", end1.tv_usec - start1.tv_usec);
                if (sendlen > 0) {
                    printf("sendlen =%d ,counter=%d \n ", sendlen, counter);
                    counter += 1;
                }
                else {
                    perror("send failed");
                    goto Done;
                }
            }
            //------------------------------------------------------------//
            //-----------------------------------------------------------//
            // vTaskDelay(1 / portTICK_PERIOD_MS);

            gettimeofday(&end, NULL);
            printf("                 one frame %ld us\n", end.tv_usec - start.tv_usec);
        }
    Done:
        gettimeofday(&end_total, NULL);
        printf("one---------- time %lf ms\n", (end_total.tv_sec - start_total.tv_sec) * 1000.0 + (end_total.tv_usec - start_total.tv_usec) / 1000.0);
        opus_encoder_destroy(enc);

        ESP_LOGI(TAG, "Exit Decoding.\n");
        //延迟一秒
        vTaskDelay(4000 / portTICK_PERIOD_MS);

        shutdown(sock, 0);
        close(sock);
        // do_retransmit(sock);

    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

// void do_decode(const int sock) {

// }

void i2s_config_proc() {
    // i2s config for writing both channels of I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,//I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 3,
        .dma_buf_len = 1024,
        .use_apll = 0,
        .tx_desc_auto_clear = 0,
        //.fixed_mclk = 4096000
    };
    // i2s pinout
    static const i2s_pin_config_t pin_config = {
        .bck_io_num = 18,
        .ws_io_num = 19,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = 8
    };


    // install and start i2s driver
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config));
    //ESP_ERROR_CHECK(i2s_set_clk(I2S_NUM_0, RATE, I2S_BITS_PER_SAMPLE_16BIT,I2S_CHANNEL_STEREO));// I2S_CHANNEL_MONO));
    // enable the DAC channels
    // i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
    // clear the DMA buffers
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_NUM_0));

    ESP_ERROR_CHECK(i2s_start(I2S_NUM_0));
}

OpusEncoder *encoder_init(opus_int32 sampling_rate,
    int channels,
    int application) {
    int enc_err;
    printf("Here the rate is %ld \n", sampling_rate);
    OpusEncoder *enc =
        opus_encoder_create(sampling_rate, channels, application, &enc_err);
    if (enc_err == OPUS_ALLOC_FAIL) { printf("OPUS_ALLOC_FAIL error\n"); }
    else if (enc_err == OPUS_BAD_ARG) { printf("OPUS_BAD_ARG error\n"); }
    else if (enc_err == OPUS_BUFFER_TOO_SMALL) { printf("OPUS_BUFFER_TOO_SMALL error\n"); }
    else if (enc_err == OPUS_INTERNAL_ERROR) { printf("OPUS_INTERNAL_ERROR error\n"); }
    else if (enc_err == OPUS_INVALID_PACKET) { printf("OPUS_INVALID_PACKET error\n"); }
    else if (enc_err == OPUS_INVALID_STATE) { printf("OPUS_INVALID_STATE error\n"); }
    else if (enc_err == OPUS_UNIMPLEMENTED) { printf("OPUS_UNIMPLEMENTED error\n"); }

    if (enc_err != OPUS_OK) {
        printf("opus_encoder_create error\n");
        fprintf(stderr, "Cannot create encoder: %s\n", opus_strerror(enc_err));
        return NULL;
    }


    int bitrate_bps = OPUS_AUTO;//sampling_rate*channels*BITS;
    int bandwidth = OPUS_BANDWIDTH_WIDEBAND;
    int use_vbr = 1;
    int cvbr = 0;
    int complexity = 0;
    int use_inbandfec = 1;
    int forcechannels = 2;
    int use_dtx = 1;
    int packet_loss_perc = 0;

    opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate_bps));
    /*
    opus_int32 a=0;
    opus_encoder_ctl(enc,OPUS_GET_BITRATE(&a));
    std::cout<<"complexity="<<a<<std::endl; */
    opus_encoder_ctl(enc, OPUS_SET_BANDWIDTH(bandwidth));
    opus_encoder_ctl(enc, OPUS_SET_MAX_BANDWIDTH(bandwidth));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));//
    opus_encoder_ctl(enc, OPUS_SET_VBR(use_vbr));       //使用动态比特率
    opus_encoder_ctl(enc, OPUS_SET_VBR_CONSTRAINT(cvbr));//不启用约束VBR，启用可以降低延迟
    /*
opus_int32 a=0;
opus_encoder_ctl(enc,OPUS_GET_COMPLEXITY(&a));
std::cout<<"complexity="<<a<<std::endl;*/   //获取到的是9
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity)); //复杂度0-10，在 CPU 复杂性和质量/比特率之间进行取舍
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(use_inbandfec));   //不使用前向纠错，只适用于LPC
    opus_encoder_ctl(enc, OPUS_SET_FORCE_CHANNELS(forcechannels));//强制双声道
    opus_encoder_ctl(enc, OPUS_SET_DTX(use_dtx));               //不使用不连续传输 (DTX)，在静音或背景噪音期间降低比特率，主要适用于voip
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(packet_loss_perc));//预期丢包，用降低比特率，来防丢包
    //opus_encoder_ctl(OPUS_SET_PREDICTION_DISABLED (0))    //默认启用预测，LPC线性预测？不启用好像每一帧都有帧头，且会降低质量

    // opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&skip));
    opus_encoder_ctl(enc, OPUS_SET_LSB_DEPTH(BITS));//被编码信号的深度，是一个提示，低于该数量的信号包含可忽略的量化或其他噪声，帮助编码器识别静音

    // IMPORTANT TO CONFIGURE DELAY
    int variable_duration = OPUS_FRAMESIZE_20_MS;
    opus_encoder_ctl(enc, OPUS_SET_EXPERT_FRAME_DURATION(variable_duration));//帧时长

    return enc;
}



