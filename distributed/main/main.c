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
      sprintf(topic, "fse2021/180113861/%s", room->valuestring);
    }

    while(true)
    {
        float temperature = 20.0 + (float)rand()/(float)(RAND_MAX/10.0);
        char message[100]; 
        sprintf(message, "temperatura1: %f", temperature);
        mqtt_envia_mensagem(topic, message);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
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
