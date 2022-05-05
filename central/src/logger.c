#include <stdio.h>
#include <time.h>
#include "logger.h"

FILE *file = NULL;

void init_file(){
    file = fopen("Logs/logs.csv", "w");
    
    fprintf(file, "data, hora, dispositivo, comando\n");
    
    fclose(file);       
}

void log_data(char *dev, char *command) {
    file = fopen("Logs/logs.csv", "a");
    time_t now;
    time(&now);
    struct tm *local = localtime(&now);

    char date_time[100];
    strftime(date_time, sizeof(date_time), "%d/%m/%Y, %H:%M:%S, ", local);
    fprintf(file, "%s", date_time);
    fprintf(file, "%s, %s\n", dev, command);
    fclose(file);
}
