#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "wifi.h"
#include "mqtt.h"
#include "dht11.h"
#include "main_interface.h"
#include "storage.h"
#include "pwm.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp32/rom/uart.h"

xSemaphoreHandle wifi_connection_semaphore;
xSemaphoreHandle mqtt_connection_semaphore;
xSemaphoreHandle message_semaphore;
xSemaphoreHandle wifi_reconnection_semaphore;

char mac_addr[50]; 
char esp_topic[100];
char room_name[50];
int input_status = 0;
bool is_dimmable = true;
int connected = false; 
int last_intensity = 50;

TaskHandle_t messages_handle;

void wifi_connect(void *args)
{
	while(true)
    {
		if(xSemaphoreTake(wifi_connection_semaphore, portMAX_DELAY))
		{
        	mqtt_start();
        }
    }
}

void wait_messages(void *args) 
{
    while(true)
    {
        if(xSemaphoreTake(message_semaphore, portMAX_DELAY))
        {
            char msg[500];
            get_message(msg);
            cJSON *root = cJSON_Parse(msg);

            char *sender = cJSON_GetObjectItem(root, "sender")->valuestring;
            sprintf(esp_topic, "fse2021/180113861/dispositivos/%s",mac_addr);

            if(strcmp(sender, "central") == 0)
            {
                char *type = cJSON_GetObjectItem(root, "type")->valuestring;
                if(strcmp(type, "status") == 0)
                {
                    input_status = cJSON_GetObjectItem(root, "status")->valueint;
                    if(input_status) {
                        last_intensity = input_status;
                    }
                    if(is_dimmable)
                    {
                        set_pwm(input_status);
                    }
                    else
                    {
                        gpio_set_level(GPIO_NUM_2, input_status);
                    }
                } 
                else if(strcmp(type, "mode") == 0)
                {
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
                    cJSON_AddItemToObject(json, "type", cJSON_CreateString("mode"));
                    cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));
                    cJSON_AddItemToObject(json, "mode", cJSON_CreateNumber(OPERATION_MODE));
                    char *text = cJSON_Print(json);
                    mqtt_send_message(esp_topic, text);
                    free(text);
                    cJSON_Delete(json);
                } 
                else if(strcmp(type, "frequencia") == 0) 
                {
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
                    cJSON_AddItemToObject(json, "type", cJSON_CreateString("frequencia"));
                    cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));
                    char *text = cJSON_Print(json);
                    mqtt_send_message(esp_topic, text);
                    free(text);
                    cJSON_Delete(json);
                }
                else if(strcmp(type, "remover") == 0)
                {
                    erase_nvs();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    mqtt_stop();
                    esp_restart();
                }
                else if(strcmp(type, "cadastro") == 0)
                {
                    char *room = cJSON_GetObjectItem(root, "room")->valuestring;
                    strcpy(room_name, room);
                    
                    cJSON_DeleteItemFromObject(root, "type");
                    cJSON_DeleteItemFromObject(root, "sender");

                    is_dimmable = cJSON_GetObjectItem(root, "is_dimmable")->valueint;

                    char * data = cJSON_Print(root);
                    write_nvs(data);
                    connected = true;
                }
            }
            cJSON_Delete(root);
        }
    }
}

void wait_button_press(void *args)
{
    gpio_reset_pin(GPIO_NUM_0);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    while(true)
    {
        int status = gpio_get_level(GPIO_NUM_0);
        if(!status)
        {
            int count = 0;
            while(gpio_get_level(GPIO_NUM_0) == status)
            {
                count++;
                vTaskDelay(100 / portTICK_PERIOD_MS);
                if(count == 30)
                {
                    break;
                }
            }
            if(count == 30)
            {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddItemToObject(json, "type", cJSON_CreateString("remover"));
                cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
                cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));

                char *text = cJSON_Print(json);
                mqtt_send_message(esp_topic, text);
                free(text);
                cJSON_Delete(json);

                erase_nvs();

                vTaskDelay(pdMS_TO_TICKS(500));
                mqtt_stop();

                esp_restart();
            }
            else
            {
                input_status = !input_status;

                cJSON *json = cJSON_CreateObject();
                cJSON_AddItemToObject(json, "type", cJSON_CreateString("status"));
                cJSON_AddItemToObject(json, "status", cJSON_CreateNumber(input_status));
                cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));
                cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));

                char *text = cJSON_Print(json);
                char status_topic[100];
                sprintf(status_topic, "fse2021/180113861/%s/estado", room_name);
                mqtt_send_message(status_topic ,text);
                free(text);
                cJSON_Delete(json);

                if(is_dimmable)
                {
                    if(input_status)
                    {
                        set_pwm(last_intensity);
                    }
                    else
                    {
                        set_pwm(0);
                    }
                }
                else 
                {
                    gpio_set_level(GPIO_NUM_2, input_status);
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void handle_server_communication(void *args)
{
    char *msg;
    char topic[100];
    
    if(xSemaphoreTake(mqtt_connection_semaphore, portMAX_DELAY))
    {   
        uint8_t derived_mac_addr[6] = {0};
        ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_STA));
        sprintf(mac_addr, "%x:%x:%x:%x:%x:%x",
                derived_mac_addr[0], derived_mac_addr[1], derived_mac_addr[2],
                derived_mac_addr[3], derived_mac_addr[4], derived_mac_addr[5]);
        sprintf(topic, "fse2021/180113861/dispositivos/%s",mac_addr);

        char data[1000];
        message_semaphore = xSemaphoreCreateBinary();

        if(read_nvs(data))
        {
            cJSON *root = cJSON_Parse(data);
            cJSON_AddItemToObject(root, "type", cJSON_CreateString("reconectar"));
            cJSON_AddItemToObject(root, "id", cJSON_CreateString(mac_addr));
            cJSON_AddItemToObject(root, "mode", cJSON_CreateNumber(OPERATION_MODE));
            cJSON_AddItemToObject(root, "sender", cJSON_CreateString("distribuido"));

            is_dimmable = cJSON_GetObjectItem(root, "is_dimmable")->valueint;

            char *text = cJSON_Print(root);
            
            mqtt_send_message(topic, text);
            strcpy(room_name, cJSON_GetObjectItem(root, "room")->valuestring);

            free(text);
            cJSON_Delete(root);
            mqtt_subscribe(topic);
            xTaskCreate(&wait_messages, "Conexão ao MQTT", 4096, NULL, 1, &messages_handle);
        }
        else 
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddItemToObject(item, "id", cJSON_CreateString(mac_addr));
            cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
            cJSON_AddItemToObject(item, "status", cJSON_CreateNumber(gpio_get_level(GPIO_NUM_2)));
            cJSON_AddItemToObject(item, "sender", cJSON_CreateString("distribuido"));
            msg = cJSON_Print(item);

            mqtt_subscribe(topic);
            xTaskCreate(&wait_messages, "Conexão ao MQTT", 4096, NULL, 1, &messages_handle);

            while(!connected) {
                mqtt_send_message(topic, msg);
                vTaskDelay(5000 / portTICK_PERIOD_MS);
            }

            cJSON_Delete(item);
            free(msg);
        }

        if(OPERATION_MODE == BATTERY_MODE)
        {
           init_battery_mode();
        }
        else
        {
            char status_topic[100];
            sprintf(status_topic, "fse2021/180113861/%s/estado", room_name);
            mqtt_subscribe(status_topic);
            xTaskCreate(&wait_button_press, "Acionamento do Botão", 4096, NULL, 1, NULL);
            send_dht_data();
        }
    }
}

void init_battery_mode()
{
    ESP_LOGI("DEBUG", "CONFIGURADO PARA BATERIA");
    vTaskDelete(messages_handle);

    gpio_pad_select_gpio(GPIO_NUM_0);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    int count = 0;
    while(true)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        mqtt_stop();
        wifi_stop();
        ESP_LOGI("SLEEP", "Entrando em modo Light Sleep\n");
        uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
        esp_light_sleep_start();

        if(rtc_gpio_get_level(GPIO_NUM_0) == 0)
        {
            count = 0;
            while(rtc_gpio_get_level(GPIO_NUM_0) == 0)
            {
                count++;
                vTaskDelay(100 / portTICK_PERIOD_MS);
                if(count == 30)
                {
                    break;
                }
            }
        }

        wifi_restart();

        if(xSemaphoreTake(wifi_reconnection_semaphore, portMAX_DELAY)){
            mqtt_restart();
        }

        if(count == 30)
        {
            ESP_LOGI("DEBUG", "REBOOT");
            cJSON *json = cJSON_CreateObject();
            cJSON_AddItemToObject(json, "type", cJSON_CreateString("remover"));
            cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
            cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));

            char *text = cJSON_Print(json);
            mqtt_send_message(esp_topic, text);
            free(text);
            cJSON_Delete(json);
            
            erase_nvs();

            vTaskDelay(pdMS_TO_TICKS(500));
            mqtt_stop();

            esp_restart();
        }
        else
        {
            input_status = !input_status;
            ESP_LOGI("DEBUG", "PREPARANDO PARA ENVIAR");
            
            cJSON *json = cJSON_CreateObject();
            cJSON_AddItemToObject(json, "type", cJSON_CreateString("status"));
            cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
            cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));
            cJSON_AddItemToObject(json, "status", cJSON_CreateNumber(input_status));

            char *text = cJSON_Print(json);

            char status_topic[100];
            sprintf(status_topic, "fse2021/180113861/%s/estado", room_name);
            mqtt_send_message(status_topic ,text);

            free(text);
            cJSON_Delete(json);
        }
    }
}

void send_dht_data()
{
	char temperature_topic[100], humidity_topic[100];
	sprintf(temperature_topic, "fse2021/180113861/%s/temperatura", room_name);
	sprintf(humidity_topic, "fse2021/180113861/%s/umidade", room_name);

    DHT11_init(GPIO_NUM_4);
    float temperature = 0, humidity = 0;
    int count = 0, valid_readings = 0; 
	while(true)
    {
        count++;
        struct dht11_reading dht11_data;
        dht11_data = DHT11_read();

        if(dht11_data.temperature != -1) {
            temperature += dht11_data.temperature;
            humidity += dht11_data.humidity;
            valid_readings++;
        }

        if(count == 5)
        {
            char *dht_temp_msg = NULL, *dht_humidity_msg = NULL;
			create_data_json(&dht_temp_msg, "temperature", temperature/valid_readings);
			create_data_json(&dht_humidity_msg, "humidity", humidity/valid_readings);

            mqtt_send_message(temperature_topic, dht_temp_msg);
            mqtt_send_message(humidity_topic, dht_humidity_msg);

            free(dht_temp_msg);
            free(dht_humidity_msg);

            temperature = 0;
            humidity = 0;
            count = 0;
            valid_readings = 0;
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void create_data_json(char **msg, char *type, float data) 
{
    cJSON *item = cJSON_CreateObject();

    cJSON_AddItemToObject(item, "type", cJSON_CreateString(type));
    cJSON_AddItemToObject(item, type, cJSON_CreateNumber(data));
    cJSON_AddItemToObject(item, "id", cJSON_CreateString(mac_addr));
    cJSON_AddItemToObject(item, "sender", cJSON_CreateString("distribuido"));

    *msg = cJSON_Print(item);

    cJSON_Delete(item);
}

void init_server()
{
    // Inicializa o NVS
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_connection_semaphore = xSemaphoreCreateBinary();
    mqtt_connection_semaphore = xSemaphoreCreateBinary();
    wifi_reconnection_semaphore = xSemaphoreCreateBinary();
    wifi_start();

    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

    xTaskCreate(&wifi_connect,  "Conexão ao MQTT", 4096, NULL, 1, NULL);
    
    xTaskCreate(&handle_server_communication, "Comunicação com Broker", 4096, NULL, 1, NULL);
}
