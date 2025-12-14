#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#define WIFI_SSID       "Robot"
#define WIFI_PASS       "12345678"
#define MAX_CONNECTIONS 1
#define TCP_PORT        8080

// MOTOR A
#define IN1_GPIO GPIO_NUM_33
#define IN2_GPIO GPIO_NUM_32
#define ENA_GPIO GPIO_NUM_14 // PWM

// MOTOR B
#define IN3_GPIO GPIO_NUM_26
#define IN4_GPIO GPIO_NUM_27
#define ENB_GPIO GPIO_NUM_25 // PWM

// MOTOR C
#define IN1_GPIO2  GPIO_NUM_16
#define IN2_GPIO2  GPIO_NUM_17
#define ENA_GPIO2  GPIO_NUM_18

// MOTOR D (STEPPER COM L298N)
#define STEP_IN1 GPIO_NUM_4
#define STEP_IN2 GPIO_NUM_5
#define STEP_IN3 GPIO_NUM_19
#define STEP_IN4 GPIO_NUM_21

// passo base 
static int stepper_speed_ms = 15;

// LIMITES DE VELOCIDADE DO STEPPER
#define STEPPER_MIN_DELAY_MS  5
#define STEPPER_MAX_DELAY_MS  40

// NEMA17 → 200 passos por volta
#define STEPPER_STEPS_PER_REV 200

// DISCO COM 8 CAVIDADES
// 200 / 8 = 25 passos por bolinha
#define STEPS_PER_BOLINHA (STEPPER_STEPS_PER_REV / 8)
#define STEPPER_START_DELAY_MS 1000

/* ============================
   MOTOR DE PASSO
   ============================ */

static const int step_sequence[4][4] = {
    {1,0,1,0},
    {0,1,1,0},
    {0,1,0,1},
    {1,0,0,1}
};

static TaskHandle_t stepperTaskHandle   = NULL;
static TaskHandle_t bolinhaTaskHandle   = NULL;
static TaskHandle_t intervaloTaskHandle = NULL;

static int stepper_running   = 0;
static int bolinha_running   = 0;
static int intervalo_running = 0;

static int bolinhas_por_segundo = 1;
static int intervalo_ms_cfg     = 1000;


typedef struct {
    gpio_num_t in1;
    gpio_num_t in2;
    gpio_num_t en;
    ledc_channel_t pwm_channel;
} Motor;

static Motor motorA = { .in1 = IN1_GPIO,  .in2 = IN2_GPIO,  .en = ENA_GPIO,  .pwm_channel = LEDC_CHANNEL_0 };
static Motor motorB = { .in1 = IN3_GPIO,  .in2 = IN4_GPIO,  .en = ENB_GPIO,  .pwm_channel = LEDC_CHANNEL_1 };
static Motor motorC = { .in1 = IN1_GPIO2, .in2 = IN2_GPIO2, .en = ENA_GPIO2, .pwm_channel = LEDC_CHANNEL_2 };

static const char *TAG = "ESP32_TCP_MOTOR";



static const char* skip_spaces(const char *p) {
    while (p != NULL && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static int parse_int_field(const char *json, const char *field, int *out_value) {
    if (json == NULL || field == NULL || out_value == NULL) {
        return 0;
    }

    const char *p = strstr(json, field);
    if (p == NULL) {
        return 0;
    }

    p += strlen(field);
    p = skip_spaces(p);
    if (p == NULL) {
        return 0;
    }

    if (*p == ':') {
        p++;
        p = skip_spaces(p);
    }

    *out_value = atoi(p);
    return 1;
}

static int parse_string_field(const char *json, const char *field, char *out, size_t out_len) {
    if (json == NULL || field == NULL || out == NULL || out_len == 0) {
        return 0;
    }

    const char *p = strstr(json, field);
    if (p == NULL) {
        return 0;
    }

    p += strlen(field); // agora aponta para o começo do valor (logo após o field)
    // Exemplo field: "\"direction\":\"" então p já está no início do valor

    const char *end = strchr(p, '"');
    if (end == NULL) {
        return 0;
    }

    size_t n = (size_t)(end - p);
    if (n >= out_len) {
        n = out_len - 1;
    }

    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

/* ============================
   MOTORES DC
   ============================ */

static void motor_init(Motor *m, const char *nome) {
    gpio_reset_pin(m->in1);
    gpio_reset_pin(m->in2);
    gpio_set_direction(m->in1, GPIO_MODE_OUTPUT);
    gpio_set_direction(m->in2, GPIO_MODE_OUTPUT);

    ledc_channel_config_t channel = {
        .gpio_num = m->en,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = m->pwm_channel,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel);

    ESP_LOGI(TAG, "%s inicializado!", nome);
}

static void motor_forwardVM(Motor *m, uint16_t speed) {
    gpio_set_level(m->in1, 1);
    gpio_set_level(m->in2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
}


static void motor_stopVM(Motor *m) {
    gpio_set_level(m->in1, 0);
    gpio_set_level(m->in2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
}


static void stepper_init(void) {
    gpio_set_direction(STEP_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(STEP_IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(STEP_IN3, GPIO_MODE_OUTPUT);
    gpio_set_direction(STEP_IN4, GPIO_MODE_OUTPUT);
}

static void stepper_apply_step(int step) {
    gpio_set_level(STEP_IN1, step_sequence[step][0]);
    gpio_set_level(STEP_IN2, step_sequence[step][1]);
    gpio_set_level(STEP_IN3, step_sequence[step][2]);
    gpio_set_level(STEP_IN4, step_sequence[step][3]);
}

static void stepper_disable_coils(void) {
    gpio_set_level(STEP_IN1, 0);
    gpio_set_level(STEP_IN2, 0);
    gpio_set_level(STEP_IN3, 0);
    gpio_set_level(STEP_IN4, 0);
}

static void stepper_task(void *pv) {
    int seq = 0;

    while (stepper_running) {
        stepper_apply_step(seq);
        seq = (seq + 1) % 4;
        vTaskDelay(pdMS_TO_TICKS(stepper_speed_ms));
    }

    stepper_disable_coils();

    stepperTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void bolinha_task(void *pv) {
    int seq = 0;

    while (bolinha_running) {

        for (int i = 0; i < STEPS_PER_BOLINHA; i++) {
            stepper_apply_step(seq);
            seq = (seq + 1) % 4;

            int step_delay_ms = stepper_speed_ms;
            if (i < 5) {
                step_delay_ms = stepper_speed_ms + 10;
            } else if (i < 10) {
                step_delay_ms = stepper_speed_ms + 5;
            }

            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }

        int intervalo_ms = 1000 / bolinhas_por_segundo;
        vTaskDelay(pdMS_TO_TICKS(intervalo_ms));
    }

    stepper_disable_coils();

    bolinhaTaskHandle = NULL;
    vTaskDelete(NULL);
}

static void bolinha_intervalo_task(void *pv) {
    int seq = 0;

    while (intervalo_running) {

        for (int i = 0; i < STEPS_PER_BOLINHA; i++) {
            stepper_apply_step(seq);
            seq = (seq + 1) % 4;

            int step_delay_ms = stepper_speed_ms;
            if (i < 5) {
                step_delay_ms = stepper_speed_ms + 10;
            } else if (i < 10) {
                step_delay_ms = stepper_speed_ms + 5;
            }

            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }

        vTaskDelay(pdMS_TO_TICKS(intervalo_ms_cfg));
    }

    stepper_disable_coils();

    intervaloTaskHandle = NULL;
    vTaskDelete(NULL);
}

/* ============================
   MODOS DO STEPPER
   ============================ */

static void motor_stepper_stop(void) {
    stepper_running = 0;
    bolinha_running = 0;
    intervalo_running = 0;

    if (stepperTaskHandle) {
        vTaskDelete(stepperTaskHandle);
        stepperTaskHandle = NULL;
    }

    if (bolinhaTaskHandle) {
        vTaskDelete(bolinhaTaskHandle);
        bolinhaTaskHandle = NULL;
    }

    if (intervaloTaskHandle) {
        vTaskDelete(intervaloTaskHandle);
        intervaloTaskHandle = NULL;
    }

    stepper_disable_coils();
}


static void motor_stepper_run(int speed) {
    if (speed <= 0) {
        motor_stepper_stop();
        return;
    }

    if (speed > 255) {
        speed = 255;
    }

    int range = STEPPER_MAX_DELAY_MS - STEPPER_MIN_DELAY_MS;
    int mappedDelay = STEPPER_MAX_DELAY_MS - (speed * range) / 255;

    if (mappedDelay < STEPPER_MIN_DELAY_MS) {
        mappedDelay = STEPPER_MIN_DELAY_MS;
    }
    if (mappedDelay > STEPPER_MAX_DELAY_MS) {
        mappedDelay = STEPPER_MAX_DELAY_MS;
    }

    stepper_speed_ms = mappedDelay;

    bolinha_running = 0;
    intervalo_running = 0;
    stepper_running = 1;

    if (stepperTaskHandle == NULL) {
        xTaskCreate(stepper_task, "stepper_task", 2048, NULL, 5, &stepperTaskHandle);
    }
}

static void motor_stepper_run_bolinhas(int qtd) {
    if (qtd <= 0) {
        bolinha_running = 0;
        return;
    }

    stepper_running = 0;
    intervalo_running = 0;

    bolinhas_por_segundo = qtd;
    bolinha_running = 1;

    if (bolinhaTaskHandle == NULL) {
        xTaskCreate(bolinha_task, "bolinha_task", 2048, NULL, 5, &bolinhaTaskHandle);
    }
}

static void motor_stepper_run_intervalo(int intervalo_ms) {
    if (intervalo_ms <= 0) {
        intervalo_running = 0;
        return;
    }

    stepper_running = 0;
    bolinha_running = 0;

    intervalo_ms_cfg = intervalo_ms;
    intervalo_running = 1;

    if (intervaloTaskHandle == NULL) {
        xTaskCreate(bolinha_intervalo_task, "bolinha_intervalo_task", 2048, NULL, 5, &intervaloTaskHandle);
    }
}

/* ============================
   WI-FI AP
   ============================ */

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "AP iniciado");
    }
}

static void wifi_init_softap(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 1,
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };

    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
}

/* ============================
   TCP SERVER
   ============================ */

static void tcp_server_task(void *pvParameters) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    char rx_buffer[512];
    char command_buffer[1024];
    int command_len = 0;

    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TCP_PORT);

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 1);

    ESP_LOGI(TAG, "TCP server iniciado na porta %d", TCP_PORT);

    while (1) {
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        ESP_LOGI(TAG, "Cliente conectado");

        command_len = 0;

        while (1) {
            int len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len <= 0) {
                ESP_LOGI(TAG, "Cliente desconectado");
                close(client_sock);
                break;
            }

            rx_buffer[len] = '\0';

            // Acumula no buffer de comandos
            for (int i = 0; i < len; i++) {
                char c = rx_buffer[i];

                if (c == '\n') {
                    command_buffer[command_len] = '\0';

                    ESP_LOGI(TAG, "JSON recebido: %s", command_buffer);

                    int motor = 0;
                    int speed = 0;
                    int bolinhas = 0;
                    int intervalo_ms = 0;
                    char direction[32] = {0};

                    parse_int_field(command_buffer, "\"motor\":", &motor);
                    parse_int_field(command_buffer, "\"speed\":", &speed);
                    parse_int_field(command_buffer, "\"bolinhas\":", &bolinhas);
                    parse_int_field(command_buffer, "\"intervalo_ms\":", &intervalo_ms);
                    parse_string_field(command_buffer, "\"direction\":\"", direction, sizeof(direction));

                    /* ============================
                       EXECUÇÃO
                       ============================ */
                    if (strcmp(direction, "stop_all") == 0) {
                        ESP_LOGI(TAG, "STOP_ALL recebido");

                        motor_stopVM(&motorA);
                        motor_stopVM(&motorB);
                        motor_stopVM(&motorC);
                        motor_stepper_stop();
                    }

                    else if (strcmp(direction, "forward") == 0) {

                        if (motor == 1) {
							motor_forwardVM(&motorA, speed);
						}
                        else if (motor == 2) {
							motor_forwardVM(&motorB, speed);
						}
                        else if (motor == 3) {
							motor_forwardVM(&motorC, speed);
						}

                        else if (motor == 4) {
                            vTaskDelay(pdMS_TO_TICKS(1500));

                            if (intervalo_ms > 0) {
                                motor_stepper_run_intervalo(intervalo_ms);
                            } else if (bolinhas > 0) {
                                motor_stepper_run_bolinhas(bolinhas);
                            } else {
                                motor_stepper_run(speed);
                            }
                        }
                    }

                    else if (strcmp(direction, "stop") == 0) {

                        if (motor == 1){
							motor_stopVM(&motorA);
						}
                        else if (motor == 2){
							 motor_stopVM(&motorB);
						}
                        else if (motor == 3){
							motor_stopVM(&motorC);
						}
                        else if (motor == 4) {
							motor_stepper_stop();
						}
                    }

                    command_len = 0;
                } 
                else {
                    if (command_len < sizeof(command_buffer) - 1) {
                        command_buffer[command_len++] = c;
                    }
                }
            }
        }
    }
}


void app_main(void) {
    nvs_flash_init();
    wifi_init_softap();

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    motor_init(&motorA, "Motor A");
    motor_init(&motorB, "Motor B");
    motor_init(&motorC, "Motor C");

    stepper_init();

    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}
