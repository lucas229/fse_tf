#ifndef MAIN_INTERFACE_H
#define MAIN_INTERFACE_H

#define BATTERY_MODE 0
#define ENERGY_MODE 1

#if defined(CONFIG_MODE_ENERGY)
#define OPERATION_MODE ENERGY_MODE
#endif

#if defined(CONFIG_MODE_BATTERY)
#define OPERATION_MODE BATTERY_MODE
#endif

void init_server();
void wifi_connect(void *args);
void wait_messages(void *args);
void handle_server_communication(void *args);
void send_dht_data(char *room_name);
void create_data_json(char **msg, char *type, float data);

#endif
