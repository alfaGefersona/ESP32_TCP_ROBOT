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

#define IN1_GPIO GPIO_NUM_33
#define IN2_GPIO GPIO_NUM_32
#define ENA_GPIO GPIO_NUM_14 // PWM


#define IN3_GPIO GPIO_NUM_26
#define IN4_GPIO GPIO_NUM_27
#define ENB_GPIO GPIO_NUM_25 // PWM

#define IN1_GPIO2  GPIO_NUM_16
#define IN2_GPIO2  GPIO_NUM_17
#define ENA_GPIO2  GPIO_NUM_18

typedef struct {
    gpio_num_t in1;
    gpio_num_t in2;
    gpio_num_t en;
    ledc_channel_t pwm_channel;
} Motor;

Motor motorA = { .in1 = IN1_GPIO, .in2 = IN2_GPIO, .en = ENA_GPIO, .pwm_channel = LEDC_CHANNEL_0 };
Motor motorB = { .in1 = IN3_GPIO, .in2 = IN4_GPIO, .en = ENB_GPIO, .pwm_channel = LEDC_CHANNEL_1 };
Motor motorC = { .in1 = IN1_GPIO2, .in2 = IN2_GPIO2, .en = ENA_GPIO2, .pwm_channel = LEDC_CHANNEL_2 };
Motor motorD ={}; 

static const char *TAG = "ESP32_TCP_MOTOR";

/* ============================
   CONFIGURAÇÃO DO MOTOR
   ============================ */

void motor_init(Motor *m, const char *nome) {
    ESP_LOGI("MOTOR_INIT", "inicializando %s...", nome);

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

    ESP_LOGI("MOTOR_INIT", "canal PWM configurado: EN=%d | Canal=%d", m->en, m->pwm_channel);
    ESP_LOGI("MOTOR_INIT", "%s inicializado com sucesso!\n", nome);
}


    //sentido horário
void motor_forwardVM(Motor *m, uint16_t speed) {
    gpio_set_level(m->in1, 1);
    gpio_set_level(m->in2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
    ESP_LOGI(TAG, "motor frente (speed=%d)", speed);
}

	//sentido anti-horário
void motor_backwardVM(Motor *m, uint16_t speed) {
    gpio_set_level(m->in1, 0);
    gpio_set_level(m->in2, 1);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, speed);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
    ESP_LOGI(TAG, "motor re (speed=%d)", speed);
}

    // para o motor
void motor_stopVM(Motor *m) {
    gpio_set_level(m->in1, 0);
    gpio_set_level(m->in2, 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, m->pwm_channel);
    ESP_LOGI(TAG, "motor parado");
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
        ESP_LOGI(TAG, "AP iniciado (SSID: %s)", WIFI_SSID);
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "Dispositivo conectado");
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "Dispositivo desconectado");
        break;
    default:
        break;
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 1,
            .password = WIFI_PASS,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK}};
    if (strlen(WIFI_PASS) == 0)
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP iniciado (IP => 192.168.4.1)");
}

/* ============================
   SERVIDOR TCP SOCKET
   ============================ */
void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char rx_buffer[128];

    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (server_sock < 0)
    {
        ESP_LOGE(TAG, "Falha ao criar socket");
        vTaskDelete(NULL);
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TCP_PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        ESP_LOGE(TAG, "Erro no bind()");
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_sock, 1) < 0)
    {
        ESP_LOGE(TAG, "Erro no listen()");
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Servidor TCP ativo na porta %d", TCP_PORT);

    while (1)
    {
        ESP_LOGI(TAG, "Aguardando cliente...");
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0)
        {
            ESP_LOGE(TAG, " Erro no accept()");
            continue;
        }

        ESP_LOGI(TAG, "Cliente conectado");

       while (1)
		{
		    int len = recv(client_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
		    if (len <= 0)
		    {
		        ESP_LOGI(TAG, "cliente desconectado");
		        close(client_sock);
		        break;
		    }
		
		    rx_buffer[len] = '\0';
		    ESP_LOGI(TAG, "recebido: %s", rx_buffer);
		
		    int speed = 0;
		    char direction[32] = {0};

			int motor = 0;
			
			// extrai o campo motor
			char *motor_ptr = strstr(rx_buffer, "\"motor\":");
			if (motor_ptr)
			{
			    motor_ptr += 8; // pula "motor":
			    motor = atoi(motor_ptr);
			}

		
		    // extrai o campo speed da msg
		    char *speed_ptr = strstr(rx_buffer, "\"speed\":");
		    if (speed_ptr)
		    {
		        speed_ptr += 8; // pula o texto "speed":
		        speed = atoi(speed_ptr);
		    }
		
		    // extrai o campo direction da msg
		    char *dir_ptr = strstr(rx_buffer, "\"direction\":\"");
		    if (dir_ptr)
		    {
		        dir_ptr += 13; // pula o texto "direction":" 
		        char *end_quote = strchr(dir_ptr, '"');
		        if (end_quote)
		        {
		            size_t len_dir = end_quote - dir_ptr;
		            if (len_dir < sizeof(direction))
		            {
		                strncpy(direction, dir_ptr, len_dir);
		                direction[len_dir] = '\0';
		            }
		        }
		    }
		
		    ESP_LOGI(TAG, "motor: %d | direcao: %s | velocidade: %d", motor, direction, speed);
			
			if (strcmp(direction, "forward") == 0)
			{
			    if (motor == 1)
			        motor_forwardVM(&motorA, speed);
			    else if (motor == 2)
			        motor_forwardVM(&motorB, speed);
 				else if (motor == 3)
			        motor_forwardVM(&motorC, speed);
			
			    send(client_sock, "frente\n", 10, 0);
			}
			else if (strcmp(direction, "backward") == 0)
			{
			    if (motor == 1)
			        motor_backwardVM(&motorA, speed);
			    else if (motor == 2)
			        motor_backwardVM(&motorB, speed);
			    else if (motor == 3)
			        motor_backwardVM(&motorC, speed);
			
			    send(client_sock, "re\n", 8, 0);
			}
			else if (strcmp(direction, "stop") == 0)
			{
			    if (motor == 1)
			        motor_stopVM(&motorA);
			    else if (motor == 2)
			        motor_stopVM(&motorB);
				else if (motor == 3)
			        motor_stopVM(&motorC);
			    send(client_sock, "parado\n", 11, 0);
			}else if (strcmp(direction, "stop_all") == 0)
			{
			    motor_stopVM(&motorA);
			    motor_stopVM(&motorB);
				motor_stopVM(&motorC);
			    send(client_sock, "todos motores parados\n", 26, 0);
			    ESP_LOGI(TAG, "todos os motores parados");
			}

			else
			{
			    send(client_sock, "comando inválido\n", 22, 0);
			}

		}

    }

    close(server_sock);
    vTaskDelete(NULL);
}



/* ============================
   MAIN
   ============================ */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_softap();

    // configura o timer pwm
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer);

    // inicializa os componentes (motor)
    motor_init(&motorA, "Motor A (OUT1/OUT2)");
    motor_init(&motorB, "Motor B (OUT3/OUT4)");
    motor_init(&motorC, "Motor C (OUT1/OUT2)");


    // inicia o server TCP
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}


