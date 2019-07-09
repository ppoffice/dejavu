#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/input"

static FILE* in;
static char** devices;
static int* device_fds;
static int device_count = 0;

static void usage(char* name) {
    fprintf(stderr, "Usage: %s input\n", name);
}

static void trim(char* s) {
    char* p = s;
    int l = strlen(p);

    while (isspace(p[l - 1])) p[--l] = 0;
    while (*p && isspace(*p)) ++p, --l;

    memmove(s, p, l + 1);
}

static inline long long int get_current_time() {
    struct timespec cur_time;
    clock_gettime(CLOCK_MONOTONIC, &cur_time);
    return (cur_time.tv_sec * 1E9 + cur_time.tv_nsec) / 1E3;
}

static void sleep_to(long long int target_time) {
    struct timespec delay;

    delay.tv_sec = 0;
    delay.tv_nsec = 1000;

    while (errno != EINTR && get_current_time() < target_time) {
        nanosleep(&delay, NULL);
    }
}

static int open_device(const char* device) {
    int fd;
    char** devices_tmp;
    int* device_fds_tmp;
    char filename[256];

    for (int i = 0; i < device_count; i++) {
        if (!strcmp(devices[i], device)) {
            return device_fds[i];
        }
    }

    devices_tmp = realloc(devices, sizeof(char*) * (device_count + 1));
    device_fds_tmp = realloc(device_fds, sizeof(int) * (device_count + 1));
    if (devices_tmp == NULL || device_fds_tmp == NULL) {
        fprintf(stderr, "out of memory\n");
        close(fd);
        return -1;
    }

    snprintf(filename, sizeof(filename) * sizeof(char), "%s/%s", DEVICE_PATH, device);
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "could not open %s, %s\n", filename, strerror(errno));
        return -1;
    }

    devices = devices_tmp;
    device_fds = device_fds_tmp;
    devices[device_count] = strdup(device);
    device_fds[device_count] = fd;
    device_count++;

    return fd;
}

static void close_devices() {
    for (int i = 0; i < device_count; i++) {
        free(devices[i]);
        close(device_fds[i]);
    }
}

int main(int argc, char* argv[]) {
    int fd;
    int read;
    size_t len;
    char* line;
    char* line_trimed;
    char* device;
    const char* in_file;
    long long int interval;
    long long int start_time;
    struct input_event event;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    in_file = argv[1];
    in = fopen(in_file, "r");
    if (in == NULL) {
        fprintf(stderr, "failed to open output file %s, %s\n", in_file, strerror(errno));
        return 1;
    }

    start_time = get_current_time();
    while ((read = getdelim(&line, &len, '\n', in)) != -1) {
        trim(line);
        interval = atoll(strtok(line, " "));
        device = strtok(NULL, " ");

        memset(&event, 0, sizeof(event));
        event.type = atoi(strtok(NULL, " "));
        event.code = atoi(strtok(NULL, " "));
        event.value = atoi(strtok(NULL, " "));

        fd = open_device(device);
        if (fd < 0) {
            return 1;
        }

        start_time += interval;
        sleep_to(start_time);

        if (write(fd, &event, sizeof(event)) < sizeof(event)) {
            fprintf(stderr, "write event failed, %s\n", strerror(errno));
            return 1;
        }
    }

    if (line) {
        free(line);
    }
    fclose(in);
    close_devices();
    return 0;
}