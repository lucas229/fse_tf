#ifndef MAIN_INTERFACE_H
#define MAIN_INTERFACE_H

void init_server();
void wifi_connect(void *args);
void wait_messages(void *args);
void handle_server_communication(void *args);
void send_dht_data(char *room_name);
void create_data_json(char **msg, char *type, float data);

#endif
