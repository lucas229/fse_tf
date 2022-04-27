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

void trataComunicacaoComServidor(void * params)
{
  char *msg;
  char topic[100];
  if(xSemaphoreTake(conexaoMQTTSemaphore, portMAX_DELAY))
  {
    uint8_t derived_mac_addr[6] = {0};
    char mac_addr[50]; 
    ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_STA));
    sprintf(mac_addr, "%x:%x:%x:%x:%x:%x",
             derived_mac_addr[0], derived_mac_addr[1], derived_mac_addr[2],
             derived_mac_addr[3], derived_mac_addr[4], derived_mac_addr[5]);
    sprintf(topic, "fse2021/180113861/dispositivos/%s",mac_addr);

    cJSON *item = cJSON_CreateObject();
    cJSON_AddItemToObject(item, "id", cJSON_CreateString(mac_addr));
    cJSON_AddItemToObject(item, "type", cJSON_CreateString("Cadastro"));
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
      sprintf(topic, "fse2021/180113861/%s/temperatura", room->valuestring);
    }

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

          cJSON *dht_message = cJSON_CreateObject();
          cJSON_AddItemToObject(dht_message, "type", cJSON_CreateString("Info"));
          cJSON_AddItemToObject(dht_message, "temperature", cJSON_CreateNumber(temperature / valid_readings));
          cJSON_AddItemToObject(dht_message, "humidity", cJSON_CreateNumber(humidity/valid_readings));
          
          char *dht_msg = cJSON_Print(dht_message);
          mqtt_envia_mensagem(topic, dht_msg);
          free(dht_msg);
          cJSON_Delete(dht_message);

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

    xTaskCreate(&conectadoWifi,  "Conexão ao MQTT", 4096, NULL, 1, NULL);
    
    xTaskCreate(&trataComunicacaoComServidor, "Comunicação com Broker", 4096, NULL, 1, NULL);

}
