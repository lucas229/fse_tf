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
} Device;

int init_server();
void init_client(const char *topic);
void exit_server();
void publish_callback(void **unused, struct mqtt_response_publish *published);
void *client_refresher(void *client);
void register_device(struct mqtt_response_publish *published);
void handle_device_data(struct mqtt_response_publish *published, char *type);
int find_device_by_mac(char *mac_addr);
void show_status_menu();
void change_status_menu();
void *init_menu(void *args);

#endif
