#ifndef MAIN_INTERFACE_H
#define MAIN_INTERFACE_H

#include <pthread.h>

#include "mqtt.h"

#define MAX_DEVICES 10

typedef struct Device {
    char id[20];
    int status;
    float temperature;
    float humidity;
    int is_active; 
} Device;

void init_server();
void init_client(const char *topic);
void exit_server();
void publish_callback(void **unused, struct mqtt_response_publish *published);
void *client_refresher(void *client);
void handle_register_request(struct mqtt_response_publish *published);
void register_device(char *room_name, char* device_id);
void handle_device_data(struct mqtt_response_publish *published, char *type);
int find_device_by_mac(char *mac_addr);
void show_status_menu();
void change_status_menu();
void *check_frequence(void *args);
void *init_menu(void *args);
void menu_register();

#endif
