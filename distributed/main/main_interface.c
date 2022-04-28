#include <stdio.h>
#include <string.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "wifi.h"
#include "mqtt.h"
#include "dht11.h"
#include "main_interface.h"

xSemaphoreHandle wifi_connection_semaphore;
xSemaphoreHandle mqtt_connection_semaphore;
xSemaphoreHandle message_semaphore;
char mac_addr[50]; 

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
            int status = cJSON_GetObjectItem(root, "status")->valueint;
            gpio_set_level(GPIO_NUM_2, status);
            cJSON_Delete(root);
        }
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

        cJSON *item = cJSON_CreateObject();
        cJSON_AddItemToObject(item, "id", cJSON_CreateString(mac_addr));
        cJSON_AddItemToObject(item, "type", cJSON_CreateString("cadastro"));
        cJSON_AddItemToObject(item, "status", cJSON_CreateNumber(gpio_get_level(GPIO_NUM_2)));
        msg = cJSON_Print(item);
		printf("%s\n", msg);
        mqtt_envia_mensagem(topic, msg);  
        cJSON_Delete(item);
        free(msg);

        mqtt_inscrever(topic);
        message_semaphore = xSemaphoreCreateBinary();
        if(xSemaphoreTake(message_semaphore, portMAX_DELAY)) {
            char json_message[100];
            obter_mensagem(json_message);
            cJSON *root = cJSON_Parse(json_message);
            cJSON *room = cJSON_GetObjectItem(root, "comodo");
			strcpy(room_name, room->valuestring);
        }

        xTaskCreate(&wait_messages, "Conexão ao MQTT", 4096, NULL, 1, NULL);

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

    xTaskCreate(&wifi_connect,  "Conexão ao MQTT", 4096, NULL, 1, NULL);
    
    xTaskCreate(&handle_server_communication, "Comunicação com Broker", 4096, NULL, 1, NULL);
}