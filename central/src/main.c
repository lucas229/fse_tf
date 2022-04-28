#include <signal.h>

#include "main_interface.h"

void handle_signal(int signal) {
    exit_server();
}

int main() {
    signal(SIGINT, handle_signal);
    init_server();
    return 0;
}
