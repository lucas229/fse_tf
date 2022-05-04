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

xSemaphoreHandle wifi_connection_semaphore;
xSemaphoreHandle mqtt_connection_semaphore;
xSemaphoreHandle message_semaphore;
xSemaphoreHandle reconnect_semaphore;

char mac_addr[50]; 
TaskHandle_t *const wifi_task, server_task; 

void wifi_connect(void *args)
{
	while(true)
    {
		if(xSemaphoreTake(wifi_connection_semaphore, portMAX_DELAY))
		{
        // Processamento Internet
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
            char msg[100];
            obter_mensagem(msg);
            cJSON *root = cJSON_Parse(msg);

            char *sender = cJSON_GetObjectItem(root, "sender")->valuestring;
            if(strcmp(sender, "central") == 0)
            {
                char *type = cJSON_GetObjectItem(root, "type")->valuestring;
                if(strcmp(type, "status") == 0)
                {
                    int status = cJSON_GetObjectItem(root, "status")->valueint;
                    gpio_set_level(GPIO_NUM_2, status);
                } else if(strcmp(type, "mode") == 0)
                {
                    cJSON *json = cJSON_CreateObject();
                    char esp_topic[100];
                    sprintf(esp_topic, "fse2021/180113861/dispositivos/%s",mac_addr);
                    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
                    cJSON_AddItemToObject(json, "type", cJSON_CreateString("mode"));
                    cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));
                    cJSON_AddItemToObject(json, "mode", cJSON_CreateNumber(OPERATION_MODE));
                    char *text = cJSON_Print(json);
                    mqtt_envia_mensagem(esp_topic, text);
                    free(text);
                    cJSON_Delete(json);
                } else if(strcmp(type, "frequencia") == 0) 
                {
                    cJSON *json = cJSON_CreateObject();
                    char esp_topic[100];
                    sprintf(esp_topic, "fse2021/180113861/dispositivos/%s",mac_addr);
                    cJSON_AddItemToObject(json, "sender", cJSON_CreateString("distribuido"));
                    cJSON_AddItemToObject(json, "type", cJSON_CreateString("frequencia"));
                    cJSON_AddItemToObject(json, "id", cJSON_CreateString(mac_addr));
                    char *text = cJSON_Print(json);
                    mqtt_envia_mensagem(esp_topic, text);
                    free(text);
                    cJSON_Delete(json);
                } else if(strcmp(type, "remove") == 0) 
                {
                    vTaskDelete(wifi_task);
                    vTaskDelete(server_task);
                    reconnect_semaphore = xSemaphoreCreateBinary();
                    if(xSemaphoreTake(reconnect_semaphore, portMAX_DELAY)) {
                        esp_restart();
                    }
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

    int count = 0;
    while(true)
    {
		int status = gpio_get_level(GPIO_NUM_0);
        if(!status)
        {
            count++;
        }
        else
        {
            count = 0;
        }

        if(count == 3)
        {
            xSemaphoreGive(reconnect_semaphore);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void handle_server_communication(void *args)
{
    char *msg;
    char topic[100];
	char room_name[50];
    
    if(xSemaphoreTake(mqtt_connection_semaphore, portMAX_DELAY))
    {   
        uint8_t derived_mac_addr[6] = {0};
        ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_STA));
        sprintf(mac_addr, "%x:%x:%x:%x:%x:%x",
                derived_mac_addr[0], derived_mac_addr[1], derived_mac_addr[2],
                derived_mac_addr[3], derived_mac_addr[4], derived_mac_addr[5]);
        sprintf(topic, "fse2021/180113861/dispositivos/%s",mac_addr);

        char data[500];
        message_semaphore = xSemaphoreCreateBinary();

        if(read_nvs(data))
        {
            cJSON *root = cJSON_Parse(data);
            cJSON_AddItemToObject(root, "type", cJSON_CreateString("reconectar"));
            cJSON_AddItemToObject(root, "id", cJSON_CreateString(mac_addr));
            cJSON_AddItemToObject(root, "mode", cJSON_CreateNumber(OPERATION_MODE));
            cJSON_AddItemToObject(root, "sender", cJSON_CreateString("distribuido"));
            char *text = cJSON_Print(root);
            
            mqtt_envia_mensagem(topic, text);
            strcpy(room_name, cJSON_GetObjectItem(root, "room")->valuestring);

            free(text);
            cJSON_Delete(root);
            mqtt_inscrever(topic);
        }
        else 
        {
            
            cJSON *item = cJSON_CreateObject();
            cJSON_AddItemToObject(item, "id", cJSON_CreateString(mac_addr));
            cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
            cJSON_AddItemToObject(item, "status", cJSON_CreateNumber(gpio_get_level(GPIO_NUM_2)));
            cJSON_AddItemToObject(item, "sender", cJSON_CreateString("distribuido"));
            msg = cJSON_Print(item);
            mqtt_envia_mensagem(topic, msg);  
            cJSON_Delete(item);
            free(msg);

            mqtt_inscrever(topic);
            while(1) 
            {
                if(xSemaphoreTake(message_semaphore, portMAX_DELAY)) 
                {
                    char json_message[1000];
                    obter_mensagem(json_message);
                    cJSON *root = cJSON_Parse(json_message);
                    char *type = cJSON_GetObjectItem(root, "type")->valuestring;
                    if(strcmp(type, "cadastro") == 0){
                        char * data;
                        char *room = cJSON_GetObjectItem(root, "room")->valuestring;
                        strcpy(room_name, room);

                        cJSON_DeleteItemFromObject(root, "type");
                        cJSON_DeleteItemFromObject(root, "sender");
                        data = cJSON_Print(root);
                        write_nvs(data);
                        cJSON_Delete(root);
                        break;
                    }
                    cJSON_Delete(root);
                }
            }
        }

        xTaskCreate(&wait_messages, "Conexão ao MQTT", 4096, NULL, 1, NULL);
        xTaskCreate(&wait_button_press, "Acionamento do Botão", 4096, NULL, 1, NULL);

		send_dht_data(room_name);
    }
}

void send_dht_data(char *room_name)
{
	char temperature_topic[100], humidity_topic[100], status_topic[100];
	sprintf(temperature_topic, "fse2021/180113861/%s/temperatura", room_name);
	sprintf(status_topic, "fse2021/180113861/%s/estado", room_name);
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

            mqtt_envia_mensagem(temperature_topic, dht_temp_msg);
            mqtt_envia_mensagem(humidity_topic, dht_humidity_msg);

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
    wifi_start();

    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

    xTaskCreate(&wifi_connect,  "Conexão ao MQTT", 4096, NULL, 1, wifi_task);
    
    xTaskCreate(&handle_server_communication, "Comunicação com Broker", 4096, NULL, 1, server_task);
}
