#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "storage.h"

int read_nvs(char *data) 
{
    ESP_ERROR_CHECK(nvs_flash_init());

    nvs_handle default_partition_handle;

    esp_err_t res_nvs = nvs_open("storage", NVS_READONLY, &default_partition_handle);
    
    if(res_nvs == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "Namespace: armazenamento, não encontrado");
    }
    else
    {
        size_t length = 4000;
        esp_err_t res = nvs_get_str(default_partition_handle, "configs", data, &length);

        switch (res)
        {
        case ESP_OK:
            return 1;    
        case ESP_ERR_NOT_FOUND:
            ESP_LOGI("NVS", "Valor não encontrado");
            return 0;
        default:
            ESP_LOGI("NVS", "Erro ao acessar o NVS (%s)", esp_err_to_name(res));
            return 0;
        }

        nvs_close(default_partition_handle);
    }
    return 0;
}

void write_nvs(char *data)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    nvs_handle default_partition_handle;

    esp_err_t res_nvs = nvs_open("storage", NVS_READWRITE, &default_partition_handle);
    
    if(res_nvs == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "Namespace: armazenamento, não encontrado");
    }
  
    esp_err_t res = nvs_set_str(default_partition_handle, "configs", data);

    if(res != ESP_OK)
    {
        ESP_LOGI("NVS", "Não foi possível escrever no NVS (%s)", esp_err_to_name(res));
    }
    nvs_commit(default_partition_handle);
    nvs_close(default_partition_handle);
}

void erase_nvs(){
    ESP_ERROR_CHECK(nvs_flash_init());

    nvs_handle default_partition_handle;

    nvs_open("storage", NVS_READWRITE, &default_partition_handle);

    nvs_erase_all(default_partition_handle);
}
