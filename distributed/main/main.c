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

xSemaphoreHandle conexaoWifiSemaphore;
xSemaphoreHandle conexaoMQTTSemaphore;
xSemaphoreHandle recebeuMensagem; 
char mac_addr[50]; 

void conectadoWifi(void * params)
{
  while(true)
  {
    if(xSemaphoreTake(conexaoWifiSemaphore, portMAX_DELAY))
    {
      // Processamento Internet
      mqtt_start();
    }
  }
}

void waitMessages(void *args) {
  while(true)
  {
    if(xSemaphoreTake(recebeuMensagem, portMAX_DELAY))
    {
      char msg[100];
      obter_mensagem(msg);
      cJSON *root = cJSON_Parse(msg);
      int status = cJSON_GetObjectItem(root, "status")->valueint;
      gpio_set_level(GPIO_NUM_2, status);
      printf("%s\n", msg);
      cJSON_Delete(root);
    }
  }
}

void trataComunicacaoComServidor(void * params)
{
  char *msg;
  char topic[100];
  char temperature_topic[100];
  char humidity_topic[100];
  char status_topic[100];
  if(xSemaphoreTake(conexaoMQTTSemaphore, portMAX_DELAY))
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
    mqtt_envia_mensagem(topic, msg);  
    cJSON_Delete(item);
    free(msg);

    mqtt_inscrever(topic);
    recebeuMensagem = xSemaphoreCreateBinary();
    if(xSemaphoreTake(recebeuMensagem, portMAX_DELAY)) {
      char json_message[100];
      obter_mensagem(json_message);
      cJSON *root = cJSON_Parse(json_message);
      cJSON *room = cJSON_GetObjectItem(root, "comodo");
      sprintf(temperature_topic, "fse2021/180113861/%s/temperatura", room->valuestring);
      sprintf(status_topic, "fse2021/180113861/%s/estado", room->valuestring);
      sprintf(humidity_topic, "fse2021/180113861/%s/umidade", room->valuestring);
    }

    xTaskCreate(&waitMessages, "Conexão ao MQTT", 4096, NULL, 1, NULL);

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

        if(count == 5){

          cJSON *dht_temperature = cJSON_CreateObject();
          cJSON *dht_humidity = cJSON_CreateObject();

          cJSON_AddItemToObject(dht_temperature, "type", cJSON_CreateString("temperature"));
          cJSON_AddItemToObject(dht_temperature, "temperature", cJSON_CreateNumber(temperature / valid_readings));
          cJSON_AddItemToObject(dht_temperature, "id", cJSON_CreateString(mac_addr));


          cJSON_AddItemToObject(dht_humidity, "type" ,cJSON_CreateString("humidity"));
          cJSON_AddItemToObject(dht_humidity, "humidity", cJSON_CreateNumber(humidity/valid_readings));
          cJSON_AddItemToObject(dht_humidity, "id", cJSON_CreateString(mac_addr));

          char *dht_temp_msg = cJSON_Print(dht_temperature);
          char *dht_humidity_msg = cJSON_Print(dht_humidity);
          mqtt_envia_mensagem(temperature_topic, dht_temp_msg);
          mqtt_envia_mensagem(humidity_topic, dht_humidity_msg);
          
          free(dht_temp_msg);
          free(dht_humidity_msg);
          
          cJSON_Delete(dht_temperature);
          cJSON_Delete(dht_humidity);

          temperature = 0;
          humidity = 0;
          count = 0;
          valid_readings = 0;
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);   
    }
  }
}

void app_main(void)
{
    // Inicializa o NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    conexaoWifiSemaphore = xSemaphoreCreateBinary();
    conexaoMQTTSemaphore = xSemaphoreCreateBinary();
    wifi_start();

    gpio_reset_pin(GPIO_NUM_2);
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

    xTaskCreate(&conectadoWifi,  "Conexão ao MQTT", 4096, NULL, 1, NULL);
    
    xTaskCreate(&trataComunicacaoComServidor, "Comunicação com Broker", 4096, NULL, 1, NULL);

}
