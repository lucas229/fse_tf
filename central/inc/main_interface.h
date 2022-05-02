#ifndef MAIN_INTERFACE_H
#define MAIN_INTERFACE_H

#include <pthread.h>

#include "mqtt.h"

#define MAX_DEVICES 10
#define MAX_ROOMS 10
#define ENERGY_ID 0
#define BATTERY_ID 1

typedef struct Device {
    char id[20];
    char name[50];
    int status;
    int room; 
    int mode;
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
void register_device(char *room_name, char* device_id, char *device_name, int esp_mode);
void handle_device_data(struct mqtt_response_publish *published, char *type);
int find_device_by_mac(char *mac_addr);
void print_device(char* mac_addr);
void *check_frequence(void *args);
void *init_menu(void *args);
void menu_register();
void room_menu();
int room_selection_menu(); 
void change_status(Device *dev);
void new_room_menu();
int find_room_by_name(char *room_name);
void device_menu(Device *dev);
float find_data(int room, char type);

#endif
