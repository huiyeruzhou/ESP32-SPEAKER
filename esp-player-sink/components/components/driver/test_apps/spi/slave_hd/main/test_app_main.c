/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "unity_test_utils.h"
#include "esp_heap_caps.h"

#define TEST_MEMORY_LEAK_THRESHOLD (200)

static size_t before_free_8bit;
static size_t before_free_32bit;

void setUp(void)
{
    before_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    before_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
}

void tearDown(void)
{
    esp_reent_cleanup();    //clean up some of the newlib's lazy allocations
    size_t after_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t after_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    printf("\n");
    unity_utils_check_leak(before_free_8bit, after_free_8bit, "8BIT", TEST_MEMORY_LEAK_THRESHOLD);
    unity_utils_check_leak(before_free_32bit, after_free_32bit, "32BIT", TEST_MEMORY_LEAK_THRESHOLD);
}

void app_main(void)
{
    //  ____  ____ ___   ____  _                   _   _ ____
    // / ___||  _ \_ _| / ___|| | __ ___   _____  | | | |  _ `.
    // \___ \| |_) | |  \___ \| |/ _` \ \ / / _ \ | |_| | | | |
    //  ___) |  __/| |   ___) | | (_| |\ V /  __/ |  _  | |_| |
    // |____/|_|  |___| |____/|_|\__,_| \_/ \___| |_| |_|____/

    printf("\n");
    printf("     ____  ____ ___   ____  _                   _   _ ____  \n");
    printf("    / ___||  _ \\_ _| / ___|| | __ ___   _____  | | | |  _ \\ \n");
    printf("    \\___ \\| |_) | |  \\___ \\| |/ _` \\ \\ / / _ \\ | |_| | | | |\n");
    printf("     ___) |  __/| |   ___) | | (_| |\\ V /  __/ |  _  | |_| |\n");
    printf("    |____/|_|  |___| |____/|_|\\__,_| \\_/ \\___| |_| |_|____/ \n");

    unity_run_menu();
}
