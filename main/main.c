#include <string.h>
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


//MOTOR A
#define IN1_GPIO GPIO_NUM_33
#define IN2_GPIO GPIO_NUM_32
#define ENA_GPIO GPIO_NUM_14 // PWM

//MOTOR B
#define IN3_GPIO GPIO_NUM_26
#define IN4_GPIO GPIO_NUM_27
#define ENB_GPIO GPIO_NUM_25 // PWM

//MOTOR C
#define IN1_GPIO2  GPIO_NUM_16
#define IN2_GPIO2  GPIO_NUM_17
#define ENA_GPIO2  GPIO_NUM_18

// MOTOR D (STEPPER COM L298N)
#define STEP_IN1 GPIO_NUM_4
#define STEP_IN2 GPIO_NUM_5
#define STEP_IN3 GPIO_NUM_19
#define STEP_IN4 GPIO_NUM_21
static int stepper_speed_ms = 10;


typedef struct {
    gpio_num_t in1;
    gpio_num_t in2;
    gpio_num_t en;
    ledc_channel_t pwm_channel;
} Motor;

Motor motorA = { .in1 = IN1_GPIO, .in2 = IN2_GPIO, .en = ENA_GPIO, .pwm_channel = LEDC_CHANNEL_0 };
Motor motorB = { .in1 = IN3_GPIO, .in2 = IN4_GPIO, .en = ENB_GPIO, .pwm_channel = LEDC_CHANNEL_1 };
Motor motorC = { .in1 = IN1_GPIO2, .in2 = IN2_GPIO2, .en = ENA_GPIO2, .pwm_channel = LEDC_CHANNEL_2 };

static const char *TAG = "ESP32_TCP_MOTOR";


/* ============================
   MOTORES DC
   ============================ */

void motor_init(Motor *m, const char *nome) {
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

void motor_forwardVM(Motor *m, uint16_t speed) {
    gpio_set_level(m->in1, 1);
    gpio_set_level(m->in2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
}

void motor_backwardVM(Motor *m, uint16_t speed) {
    gpio_set_level(m->in1, 0);
    gpio_set_level(m->in2, 1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
}

void motor_stopVM(Motor *m) {
    gpio_set_level(m->in1, 0);
    gpio_set_level(m->in2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
}



/* ============================
   MOTOR DE PASSO
   ============================ */

static const int step_sequence[4][4] = {
    {1,0,1,0},
    {0,1,1,0},
    {0,1,0,1},
    {1,0,0,1}
};

static TaskHandle_t stepperTaskHandle = NULL;
static int stepper_running = 0;
#define STEPPER_DELAY_MS 10   // velocidade fixa

void stepper_init(void){
    gpio_set_direction(STEP_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(STEP_IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(STEP_IN3, GPIO_MODE_OUTPUT);
    gpio_set_direction(STEP_IN4, GPIO_MODE_OUTPUT);
}

void stepper_apply_step(int step){
    gpio_set_level(STEP_IN1, step_sequence[step][0]);
    gpio_set_level(STEP_IN2, step_sequence[step][1]);
    gpio_set_level(STEP_IN3, step_sequence[step][2]);
    gpio_set_level(STEP_IN4, step_sequence[step][3]);
}

void stepper_task(void *pv){
    int seq = 0;

    while(stepper_running){
        stepper_apply_step(seq);
        seq++;
        if (seq >= 4){
			seq = 0;
		}

        vTaskDelay(pdMS_TO_TICKS(stepper_speed_ms));
    }

    // desliga bobinas
    gpio_set_level(STEP_IN1,0);
    gpio_set_level(STEP_IN2,0);
    gpio_set_level(STEP_IN3,0);
    gpio_set_level(STEP_IN4,0);

    stepperTaskHandle = NULL;
    vTaskDelete(NULL);
}


void motor_stepper_run(int speed){
    if (speed < 10){
		speed = 10;
	}

    if (speed > 200) {
		speed = 200;	
	}

    stepper_speed_ms = speed;

    ESP_LOGI(TAG, "Stepper RUN speed=%d ms", stepper_speed_ms);

    stepper_running = 1;

    if (stepperTaskHandle == NULL)
        xTaskCreate(stepper_task, "stepper_task", 2048, NULL, 5, &stepperTaskHandle);
}

void motor_stepper_stop(void){
    ESP_LOGI(TAG, "Stepper STOP");
    stepper_running = 0;
}



/* ============================
   WI-FI AP
   ============================ */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "AP iniciado");
        break;
    }
}

void wifi_init_softap(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 1,
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK}};
    if (strlen(WIFI_PASS) == 0) wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
}



/* ============================
   TCP SERVER
   ============================ */
void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char rx_buffer[128];

    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TCP_PORT);

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 1);

    while (1)
    {
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);

        while (1)
        {
            int len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len <= 0)
            {
                close(client_sock);
                break;
            }

            rx_buffer[len] = '\0';

			ESP_LOGI(TAG, "JSON recebido %s",rx_buffer);


            int motor = 0;
			int speed = 0;
            char direction[32] = {0};

            char *motor_ptr = strstr(rx_buffer, "\"motor\":");
            char *dir_ptr   = strstr(rx_buffer, "\"direction\":\"");
            char *speed_ptr = strstr(rx_buffer, "\"speed\":");

			if (motor_ptr) {
			    motor = atoi(motor_ptr + 8);
			}
			
			if (speed_ptr) {
			    speed = atoi(speed_ptr + 8);
			}


            if (dir_ptr)
            {
                dir_ptr += 13;
                char *end = strchr(dir_ptr, '"');
                if (end)
                {
                    size_t n = end - dir_ptr;
                    strncpy(direction, dir_ptr, n);
                    direction[n] = '\0';
                }
            }


            if (strcmp(direction, "forward") == 0)
            {
                if (motor == 1) {
					motor_forwardVM(&motorA, speed);
				} 
                else if (motor == 2) {
				
					motor_forwardVM(&motorB, speed);
				}
                else if (motor == 3){
			
					motor_forwardVM(&motorC, speed);
				}
                else if (motor == 4){
					motor_stepper_run(speed);
				}
            }

        
            else if (strcmp(direction, "stop") == 0)
            {
                if (motor == 1){
					motor_stopVM(&motorA);
				}
                else if (motor == 2){
					motor_stopVM(&motorB);
				}
                else if (motor == 3){
					motor_stopVM(&motorC);
				}
                else if (motor == 4){
					motor_stepper_stop();
				}
            }

       
            else if (strcmp(direction, "stop_all") == 0)
            {
                motor_stopVM(&motorA);
                motor_stopVM(&motorB);
                motor_stopVM(&motorC);
                motor_stepper_stop();

                send(client_sock, "STOP ALL\n", 12, 0);
            }
        }
    }
}




void app_main(void)
{
    nvs_flash_init();
    wifi_init_softap();

    // configura PWM dos motores DC
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

