#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_CMD_LEN 1024
#define MAX_OUTPUT_LEN 4096

int main(int argc, char *argv[]) {
    char cmd[MAX_CMD_LEN] = "/etc/alternatives/iptables";
    char output[MAX_OUTPUT_LEN];
    FILE *fp;

    for (int i = 1; i < argc; i++) {
        strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }

    fp = popen(cmd, "r");
    if (fp == NULL) {
        exit(1);
    }

    while (fgets(output, MAX_OUTPUT_LEN, fp) != NULL) {
        if (strstr(output, "LibreNMS Daemon") == NULL) {
            printf("%s", output);
        }
    }

    pclose(fp);

    return 0;
}
