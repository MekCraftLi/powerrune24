/**
 * @file PowerRune_Armour.cpp
 * @brief 装甲板类
 * @version 0.2
 * @date 2024-02-19
 * @note 装甲板类，用于装甲板控制
 */
#include "PowerRune_Armour.h"
static const char* TAG_ARMOUR = "Armour";
// 变量初始化
LED_Strip* PowerRune_Armour::led_strip[5];
SemaphoreHandle_t PowerRune_Armour::ISR_mutex = xSemaphoreCreateBinary();
DEMUX PowerRune_Armour::demux_led             = DEMUX(DEMUX_IO, DEMUX_IO_enable);
TaskHandle_t PowerRune_Armour::LED_update_task_handle;
SemaphoreHandle_t PowerRune_Armour::LED_Strip_FSM_Semaphore;
LED_Strip_FSM_t PowerRune_Armour::state;

bool valid[10] = {true, true, true, true, true, true, true, true, true, true};

void PowerRune_Armour::clear_armour(bool refresh) {
    for (uint8_t i = 0; i < 5; i++) {
        if (i == LED_STRIP_ARM && state.mode == PRA_RUNE_BIG_MODE) continue; // 大符模式灯臂由全局渲染层管理
        demux_led = i;
        led_strip[i]->clear_pixels();
        if (refresh)
            led_strip[i]->refresh();
    }
}

// LED更新任务
void PowerRune_Armour::LED_update_task(void* pvParameter) {
    // 状态
    LED_Strip_FSM_t state_task;
    const PowerRune_Armour_config_info_t* config_info;

    // 清除所有灯效
    for (uint8_t i = 0; i < 5; i++) {
        demux_led = i;
        led_strip[i]->refresh();
    }
    while (1) {
        // 状态机，状态转移：START->IDLE->TARGET->HIT->BLINK->IDLE，TARGET->HIT和BLINK->IDLE过程之后task阻塞，等待信号量
        switch (state.LED_Strip_State) {
            case LED_STRIP_DEBUG: {
                do { // 初始化状态，使用valid变量显示损坏的装甲板
                    demux_led = LED_STRIP_MAIN_ARMOUR;
                    // led_strip[LED_STRIP_MAIN_ARMOUR]->clear_pixels();
                    for (size_t i = 0; i < 10; i++) {
                        if (!valid[i])
                            for (uint16_t j = hit_ring_cutoff[i]; j < hit_ring_cutoff[i + 1]; j++)
                                led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(j, 0, 100,
                                                                                  0); // Green
                        led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
                    }
                } while (xSemaphoreTake(LED_Strip_FSM_Semaphore, 0) == pdFALSE);
                // 转移状态
                state_task = state;
                break;
            }
            case LED_STRIP_IDLE:
                clear_armour();
                // 有进度时使用短超时，持续渲染灯臂进度；无进度时阻塞等待
                xSemaphoreTake(LED_Strip_FSM_Semaphore, state.global_progress > 0 ? pdMS_TO_TICKS(50) : portMAX_DELAY);
                // 转移状态
                state_task = state;
                break;
            case LED_STRIP_TARGET: {
                clear_armour(false);
                config_info = config->get_config_info_pt();

                // 点亮靶状图案、上下装甲板，灯臂刷新一次
                demux_led   = LED_STRIP_MAIN_ARMOUR;
                if (state_task.color == PR_RED) {
                    for (uint16_t i = 0; i < sizeof(target_pic) / sizeof(uint16_t); i++) {
                        led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(target_pic[i], config_info->brightness, 0, 0);
                    }
                } else {
                    for (uint16_t i = 0; i < sizeof(target_pic) / sizeof(uint16_t); i++) {
                        led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(target_pic[i], 0, 0, config_info->brightness);
                    }
                }
                led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
                /* 以下字段用于2024版本大能量机关程序，但是在2025版本中因灯效改变而需要注释掉
                demux_led = LED_STRIP_UPPER;
                led_strip[LED_STRIP_UPPER]->set_color(state_task.color == PR_RED ?
                config_info->brightness : 0, 0, state_task.color == PR_RED ? 0 :
                config_info->brightness); led_strip[LED_STRIP_UPPER]->refresh(); demux_led
                = LED_STRIP_LOWER; led_strip[LED_STRIP_LOWER]->set_color(state_task.color
                == PR_RED ? config_info->brightness : 0, 0, state_task.color == PR_RED ? 0
                : config_info->brightness); led_strip[LED_STRIP_LOWER]->refresh();
                */
                demux_led = LED_STRIP_ARM;
                led_strip[LED_STRIP_ARM]->refresh();
                // 开启矩阵流水灯
                uint8_t i = 0;
                do {
                    TickType_t xLastWakeTime = xTaskGetTickCount();

                    demux_led                = LED_STRIP_MATRIX;

                    if (state_task.color == PR_RED)
                        for (uint16_t j = 0; j < 165; j++) {
                            led_strip[LED_STRIP_MATRIX]->set_color_index(
                                j, single_arrow[(j + i * 5) % 25] * config_info->brightness, 0, 0);
                        }
                    else
                        for (uint16_t j = 0; j < 165; j++) {
                            led_strip[LED_STRIP_MATRIX]->set_color_index(
                                j, 0, 0, single_arrow[(j + i * 5) % 25] * config_info->brightness);
                        }
                    led_strip[LED_STRIP_MATRIX]->refresh();
                    i = (i + 1) % 5;
                    vTaskDelayUntil(&xLastWakeTime, MATRIX_REFRESH_PERIOD / portTICK_PERIOD_MS);

                } while (xSemaphoreTake(LED_Strip_FSM_Semaphore, 0) == pdFALSE);
                // 信号量释放后，重新加载state_task
                state_task = state;
                break;
            }
            case LED_STRIP_HIT: {
                config_info = config->get_config_info_pt();
                // 命中图案，大符为对应环数（10环特殊，点亮），小符为1环
                switch (state_task.mode) {
                    case PRA_RUNE_BIG_MODE: {
                        demux_led = LED_STRIP_MAIN_ARMOUR;
                        led_strip[LED_STRIP_MAIN_ARMOUR]->clear_pixels();
                        for (uint16_t j = hit_ring_cutoff[state_task.score - 1]; j < hit_ring_cutoff[state_task.score];
                             j++)
                            led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(
                                j, state_task.color == PR_RED ? config_info->brightness : 0, 0,
                                state_task.color == PR_RED ? 0 : config_info->brightness);

                        if (state_task.score == 10) {
                            // 点亮1，3，5，7，9环
                            for (uint16_t i = 0; i < 5; i++)
                                for (uint16_t j = hit_ring_cutoff[i * 2]; j < hit_ring_cutoff[i * 2 + 1]; j++)
                                    led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(
                                        j, state_task.color == PR_RED ? config_info->brightness : 0, 0,
                                        state_task.color == PR_RED ? 0 : config_info->brightness);
                        }
                        led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();

                        demux_led = LED_STRIP_MATRIX;
                        led_strip[LED_STRIP_MATRIX]->set_color(
                            state_task.color == PR_RED ? config_info->brightness_proportion_matrix : 0, 0,
                            state_task.color == PR_RED ? 0 : config_info->brightness_proportion_matrix);
                        led_strip[LED_STRIP_MATRIX]->refresh();
                        // 等待信号量
                        xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
                        // 转移状态
                        state_task = state;
                        break;
                    }
                    case PRA_RUNE_SMALL_MODE:
                        demux_led = LED_STRIP_MAIN_ARMOUR;
                        led_strip[LED_STRIP_MAIN_ARMOUR]->clear_pixels();
                        for (uint16_t j = hit_ring_cutoff[0]; j < hit_ring_cutoff[1]; j++) {
                            led_strip[LED_STRIP_MAIN_ARMOUR]->set_color_index(
                                j, state_task.color == PR_RED ? config_info->brightness : 0, 0,
                                state_task.color == PR_RED ? 0 : config_info->brightness);
                        }
                        led_strip[LED_STRIP_MAIN_ARMOUR]->refresh();
                        demux_led = LED_STRIP_ARM;
                        led_strip[LED_STRIP_ARM]->set_color(
                            state_task.color == PR_RED ? config_info->brightness_proportion_edge : 0, 0,
                            state_task.color == PR_RED ? 0 : config_info->brightness_proportion_edge);
                        led_strip[LED_STRIP_ARM]->refresh();
                        demux_led = LED_STRIP_MATRIX;
                        led_strip[LED_STRIP_MATRIX]->set_color(
                            state_task.color == PR_RED ? config_info->brightness_proportion_matrix : 0, 0,
                            state_task.color == PR_RED ? 0 : config_info->brightness_proportion_matrix);
                        led_strip[LED_STRIP_MATRIX]->refresh();
                        // 等待信号量
                        xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
                        // 转移状态
                        state_task = state;
                        break;
                }
                break;
            }
            case LED_STRIP_BLINK: {

                config_info = config->get_config_info_pt();
                // 按ID进行同步化延迟
                vTaskDelay((BLINK_DELAY * (6 - config_info->armour_id)) / portTICK_PERIOD_MS);
                // UPPER，LOWER，MATRIX，ARM闪烁十次，MAIN_ARMOUR不闪烁
                for (uint8_t i = 0; i < 10; i++) {
                    demux_led = LED_STRIP_UPPER;
                    led_strip[LED_STRIP_UPPER]->clear_pixels();
                    led_strip[LED_STRIP_UPPER]->refresh();
                    demux_led = LED_STRIP_LOWER;
                    led_strip[LED_STRIP_LOWER]->clear_pixels();
                    led_strip[LED_STRIP_LOWER]->refresh();
                    demux_led = LED_STRIP_MATRIX;
                    led_strip[LED_STRIP_MATRIX]->clear_pixels();
                    led_strip[LED_STRIP_MATRIX]->refresh();
                    demux_led = LED_STRIP_ARM;
                    led_strip[LED_STRIP_ARM]->clear_pixels();
                    led_strip[LED_STRIP_ARM]->refresh();
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    demux_led = LED_STRIP_UPPER;
                    led_strip[LED_STRIP_UPPER]->set_color(state_task.color == PR_RED ? config_info->brightness : 0, 0,
                                                          state_task.color == PR_RED ? 0 : config_info->brightness);
                    led_strip[LED_STRIP_UPPER]->refresh();
                    demux_led = LED_STRIP_LOWER;
                    led_strip[LED_STRIP_LOWER]->set_color(state_task.color == PR_RED ? config_info->brightness : 0, 0,
                                                          state_task.color == PR_RED ? 0 : config_info->brightness);
                    led_strip[LED_STRIP_LOWER]->refresh();
                    demux_led = LED_STRIP_MATRIX;
                    led_strip[LED_STRIP_MATRIX]->set_color(
                        state_task.color == PR_RED ? config_info->brightness_proportion_matrix : 0, 0,
                        state_task.color == PR_RED ? 0 : config_info->brightness_proportion_matrix);
                    led_strip[LED_STRIP_MATRIX]->refresh();
                    demux_led = LED_STRIP_ARM;
                    led_strip[LED_STRIP_ARM]->set_color(
                        state_task.color == PR_RED ? config_info->brightness_proportion_edge : 0, 0,
                        state_task.color == PR_RED ? 0 : config_info->brightness_proportion_edge);
                    led_strip[LED_STRIP_ARM]->refresh();
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                // 等待信号量
                xSemaphoreTake(LED_Strip_FSM_Semaphore, portMAX_DELAY);
                // 转移状态
                state_task = state;
                break;
            }
        }

        // ... 上方是 switch (state.LED_Strip_State) 的右大括号 ...

        // ================= 全局灯臂独立渲染层 (必须在 switch 外部) =================
        demux_led = LED_STRIP_ARM;
        led_strip[LED_STRIP_ARM]->clear_pixels();
        // 【新增】：在调用 config_info 之前，获取一次最新配置指针
        config_info = config->get_config_info_pt();
        uint8_t r_val = (state_task.color == PR_RED) ? config_info->brightness_proportion_edge : 0;
        uint8_t b_val = (state_task.color == PR_RED) ? 0 : config_info->brightness_proportion_edge;

        uint8_t progress = state_task.global_progress;

        static uint32_t flash_tick = 0;
        flash_tick++;
        bool is_flash_on = true;
        if (progress == 5 && (flash_tick / 25) % 2 == 1) {
            is_flash_on = false;
        }

        bool show_arm = true;
        // 【已修正】：判断是否处于 TARGET (待击打) 状态
        if (state_task.mode == PRA_RUNE_BIG_MODE && state_task.LED_Strip_State == LED_STRIP_TARGET) {
            show_arm = false; // 大符模式下待击打靶标强制关闭灯臂进度显示
        }

        if (progress > 0 && is_flash_on && show_arm && state_task.mode == PRA_RUNE_BIG_MODE) {
            uint8_t N = (11 * progress + 2) / 5;
            for (int i = 10; i >= 10 - N + 1; i--)
                led_strip[LED_STRIP_ARM]->set_color_index(i, r_val, 0, b_val);
            // 桥接灯：第一段与第二段之间 (S形转弯)
            for (int i = 11; i <= 14; i++)
                led_strip[LED_STRIP_ARM]->set_color_index(i, r_val, 0, b_val);
            for (int i = 15; i <= 15 + N - 1; i++)
                led_strip[LED_STRIP_ARM]->set_color_index(i, r_val, 0, b_val);
            for (int i = 37; i >= 37 - N + 1; i--)
                led_strip[LED_STRIP_ARM]->set_color_index(i, r_val, 0, b_val);
            // 桥接灯：第三段与第四段之间 (S形转弯)
            for (int i = 38; i <= 42; i++)
                led_strip[LED_STRIP_ARM]->set_color_index(i, r_val, 0, b_val);
            for (int i = 43; i <= 43 + N - 1; i++)
                led_strip[LED_STRIP_ARM]->set_color_index(i, r_val, 0, b_val);
        }

        led_strip[LED_STRIP_ARM]->refresh();
        // =========================================================================

        vTaskDelay(pdMS_TO_TICKS(20)); // 【原有延时】这里是 while(1) 循环的结束处
    }
    vTaskDelete(NULL);
}


void IRAM_ATTR PowerRune_Armour::GPIO_ISR_handler(void* arg) {
    // 操作过程中激活互斥锁，屏蔽其他中断
    if (xSemaphoreTake(ISR_mutex, 0) == pdFALSE)
        return;
    uint8_t io = (*(uint8_t*)arg);
    // 发送事件
    PRA_HIT_EVENT_DATA hit_event_data;
    hit_event_data.address = config->get_config_info_pt()->armour_id - 1;
    hit_event_data.score   = io;
    esp_event_post_to(pr_events_loop_handle, PRA, PRA_HIT_EVENT, &hit_event_data, sizeof(PRA_HIT_EVENT_DATA),
                      portMAX_DELAY);
    xTaskCreate(restart_ISR_task, "restart_ISR_task", 4096, NULL, 5, NULL);
}

void PowerRune_Armour::GPIO_init() {
    // 初始化GPIO
    gpio_config_t io_conf;
    // io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    for (uint8_t i = 0; i < 10; i++) {
        io_conf.pin_bit_mask = (1ULL << TRIGGER_IO[i]);
        gpio_config(&io_conf);
    }
}

void PowerRune_Armour::GPIO_polling_service(void* pvParameter) {
    uint8_t io_valid_state[10]      = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    uint8_t io_last_valid_state[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    uint8_t io_last_reading[10]     = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    TickType_t last_jump_time[10]   = {0};
    TickType_t bounce_time[10]      = {0};
    TickType_t activation_time[10]  = {0};
    TickType_t last_activation_time = 0;
    uint32_t bounce_count[10]       = {0}; // 消抖计数

    ESP_LOGI(TAG_ARMOUR, "GPIO polling service start");
    TickType_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    while (1) {
        TickType_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        for (uint8_t i = 0; i < 10; i++) {
            int reading = gpio_get_level(TRIGGER_IO[i]);

            // 去抖动处理
            if (reading != io_last_reading[i] && valid[i]) {
                last_jump_time[i] = current_time;
                bounce_count[i]++;
                ESP_LOGI(TAG_ARMOUR, "GPIO %d, Bounce Count: %d", (int)TRIGGER_IO[i], (int)bounce_count[i]);
                io_last_reading[i] = reading;
            }

            if ((current_time - last_jump_time[i]) > 1) // 消抖时间
            {
                if (valid[i] && (reading != io_valid_state[i])) // 有效触发
                {
                    if (reading == 0 && current_time - last_activation_time > 1000) // LOW level，触发间隔需大于1s
                    {
                        io_valid_state[i]    = reading;
                        last_activation_time = current_time;
                        activation_time[i]   = current_time;
                        ESP_LOGI(TAG_ARMOUR, "GPIO %d, Score IO %d Triggered", (int)TRIGGER_IO[i],
                                 (int)TRIGGER_IO_TO_SCORE[i]);
                    } else if (reading == 1) {
                        io_valid_state[i] = reading;
                        ESP_LOGI(TAG_ARMOUR, "GPIO %d, Score IO %d Released", (int)TRIGGER_IO[i],
                                 (int)TRIGGER_IO_TO_SCORE[i]);
                    }
                }
            }

            // 持续低电平激活检测
            if (io_valid_state[i] == 0 && current_time - activation_time[i] > 1000 && valid[i]) {
                valid[i] = false;
                ESP_LOGI(TAG_ARMOUR, "GPIO %d damaged: Low level detected for too long.", TRIGGER_IO[i]);
                io_valid_state[i] = 1;
            }

            // 跳变检测，防止键轴卡阻
            if ((current_time - bounce_time[i]) < 5000 && valid[i]) {
                if (bounce_count[i] > 10) // 5秒内跳变次数超过15次，认为是键轴卡阻
                {
                    valid[i] = false;
                    ESP_LOGI(TAG_ARMOUR, "GPIO %d damaged: Frequent bouncing detected.", TRIGGER_IO[i]);
                    io_valid_state[i] = 1;
                    bounce_count[i]   = 0;
                    bounce_time[i]    = current_time;
                }
            } else if (valid[i]) {
                bounce_count[i] = 0;
                bounce_time[i]  = current_time;
            } else if (!valid[i]) {
                bounce_count[i] = 0;
            }

            if ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) > 2000) // 预留2s供按键检测
            {
                if (io_valid_state[i] == 0 && valid[i] && io_last_valid_state[i] == 1) // 下升沿触发，保证实时性
                {
                    ESP_LOGI(TAG_ARMOUR, "GPIO %d, Score IO %d Sending event...", TRIGGER_IO[i],
                             TRIGGER_IO_TO_SCORE[i]);
                    // 发送事件
                    PRA_HIT_EVENT_DATA hit_event_data;
                    hit_event_data.address = config->get_config_info_pt()->armour_id - 1;
                    hit_event_data.score   = TRIGGER_IO_TO_SCORE[i];
                    esp_event_post_to(pr_events_loop_handle, PRA, PRA_HIT_EVENT, &hit_event_data,
                                      sizeof(PRA_HIT_EVENT_DATA), portMAX_DELAY);
                    io_last_valid_state[i] = io_valid_state[i];
                } else if (io_valid_state[i] == 1 && valid[i] && io_last_valid_state[i] == 0) {
                    io_last_valid_state[i] = io_valid_state[i];
                }
            }
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void PowerRune_Armour::GPIO_ISR_enable() {
    // 初始化GPIO ISR
    for (uint8_t i = 0; i < 10; i++) {
        gpio_set_intr_type(TRIGGER_IO[i], GPIO_INTR_NEGEDGE);
    }
}

void PowerRune_Armour::restart_ISR_task(void* pvParameter) {
    // 屏蔽1s
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // 释放信号量
    xSemaphoreGive(ISR_mutex);
    vTaskDelete(NULL);
}

// Class PowerRune_Armour 定义
PowerRune_Armour::PowerRune_Armour() {
    // 初始化GPIO
    GPIO_init();
    // 开启ISR服务
    // gpio_install_isr_service(0);
    // for (uint8_t i = 0; i < 10; i++)
    // {
    // gpio_isr_handler_add(TRIGGER_IO[i], GPIO_ISR_handler, (void
    // *)&TRIGGER_IO_TO_SCORE[i]);
    // }

    // GPIO_ISR_enable();
    // 开启GPIO轮询服务
    xTaskCreate(GPIO_polling_service, "GPIO_polling_service", 4096, NULL, 5, NULL);
    // 初始化LED_Strip
    led_strip[LED_STRIP_MAIN_ARMOUR] = new LED_Strip(STRIP_IO, 301);
    led_strip[LED_STRIP_UPPER]       = new LED_Strip(STRIP_IO, 86);
    led_strip[LED_STRIP_LOWER]       = new LED_Strip(STRIP_IO, 92);
    led_strip[LED_STRIP_ARM]         = new LED_Strip(STRIP_IO, 60);
    led_strip[LED_STRIP_MATRIX]      = new LED_Strip(STRIP_IO, 165);

    // 状态机更新信号量
    LED_Strip_FSM_Semaphore          = xSemaphoreCreateBinary();
    xSemaphoreGive(ISR_mutex);
    // 创建LED更新任务
    xTaskCreate(LED_update_task, "LED_update_task", 8192, NULL, 5, &LED_update_task_handle);

    // 注册装甲板事件处理
    ESP_ERROR_CHECK(
        esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_START_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_HIT_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_STOP_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register_with(pr_events_loop_handle, PRA, PRA_COMPLETE_EVENT, global_pr_event_handler, NULL));
    // OTA事件处理
    ESP_ERROR_CHECK(
        esp_event_handler_register_with(pr_events_loop_handle, PRC, OTA_BEGIN_EVENT, global_pr_event_handler, NULL));
    ESP_ERROR_CHECK(
        esp_event_handler_register_with(pr_events_loop_handle, PRC, OTA_COMPLETE_EVENT, global_pr_event_handler, NULL));
}

void PowerRune_Armour::trigger(RUNE_MODE mode, RUNE_COLOR color) {
    ESP_LOGI(TAG_ARMOUR, "Trigger Armour with mode: %s, color: %s", mode == PRA_RUNE_BIG_MODE ? "Big" : "Small",
             color == PR_RED ? "Red" : "Blue");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_TARGET;
    state.mode            = mode;
    state.color           = color;
    state.score           = 0;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::stop() {
    ESP_LOGI(TAG_ARMOUR, "Stop Armour");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_IDLE;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::debug() {
    ESP_LOGI(TAG_ARMOUR, "Debug Armour");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_DEBUG;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::hit(uint8_t score) {
    ESP_LOGI(TAG_ARMOUR, "Hit Armour with score: %d", score);
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_HIT;
    state.score           = score;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::blink() {
    ESP_LOGI(TAG_ARMOUR, "Activation Complete, Blink Armour");
    // 状态机更新
    state.LED_Strip_State = LED_STRIP_BLINK;
    // 释放信号量
    xSemaphoreGive(LED_Strip_FSM_Semaphore);
}

void PowerRune_Armour::global_pr_event_handler(void* handler_args, esp_event_base_t base, int32_t id,
                                               void* event_data) {
    if (base == PRA)
        switch (id) {
            case PRA_START_EVENT: {
                PRA_START_EVENT_DATA* start_event_data = (PRA_START_EVENT_DATA*)event_data;

                // 始终更新进度、颜色和模式（非靶标装甲也需要颜色信息来正确渲染灯臂）
                state.global_progress = start_event_data->global_progress;
                state.color = (RUNE_COLOR)start_event_data->color;
                state.mode = (RUNE_MODE)start_event_data->mode;

                // 地址是自己或者是0xFF时，才触发靶标点亮逻辑
                if (start_event_data->address == config->get_config_info_pt()->armour_id - 1 ||
                    start_event_data->address == 0xFF) {
                    trigger((RUNE_MODE)start_event_data->mode, (RUNE_COLOR)start_event_data->color);
                } else if (state.LED_Strip_State == LED_STRIP_IDLE && state.global_progress > 0) {
                    // 非靶标装甲收到进度更新时，唤醒LED任务以渲染灯臂进度
                    xSemaphoreGive(LED_Strip_FSM_Semaphore);
                }
                break;
            }
            case PRA_STOP_EVENT:
                // 大符模式下保留进度，使灯臂在装甲模块熄灭后仍能显示当前进度
                if (state.mode != PRA_RUNE_BIG_MODE)
                    state.global_progress = 0;
                stop();
                break;
            case PRA_HIT_EVENT: {
                if (state.LED_Strip_State == LED_STRIP_TARGET) {
                    PRA_HIT_EVENT_DATA* hit_event_data = (PRA_HIT_EVENT_DATA*)event_data;
                    hit(hit_event_data->score);
                }
                break;
            }
            case PRA_COMPLETE_EVENT: {
                blink();
                state.global_progress = 5; // 【新增】：完成时满进度
                break;
            }
        }
    else if (base == PRC) {
        switch (id) {
            case OTA_BEGIN_EVENT:
                debug();
                break;
            case OTA_COMPLETE_EVENT:
                stop();
                break;
        }
    }
}
