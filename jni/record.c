#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/input"

static FILE* out = NULL;
static int device_count = 0;
static char** devices;
static struct pollfd* pollfds;

static void usage(char* name) {
    fprintf(stderr, "Usage: %s [device, ...] output\n", name);
}

static int open_device(const char* device) {
    int fd;
    char** devices_tmp;
    struct pollfd* pollfds_tmp;
    char filename[256];

    snprintf(filename, sizeof(filename), "%s/%s", DEVICE_PATH, device);
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "could not open %s, %s\n", filename, strerror(errno));
        return -1;
    }

    devices_tmp = realloc(devices, sizeof(char*) * (device_count + 1));
    pollfds_tmp = realloc(pollfds, sizeof(struct pollfd) * (device_count + 2));
    if (devices_tmp == NULL || pollfds_tmp == NULL) {
        fprintf(stderr, "out of memory\n");
        close(fd);
        return -1;
    }

    devices = devices_tmp;
    pollfds = pollfds_tmp;
    pollfds[device_count + 1].fd = fd;
    pollfds[device_count + 1].events = POLLIN;
    devices[device_count] = strdup(device);
    device_count++;

    return 0;
}

static int close_device(const char* device) {
    int count;
    for (int i = 0; i < device_count; i++) {
        if (!strcmp(devices[i], device)) {
            count = device_count - i - 1;
            free(devices[i]);
            memmove(devices + i, devices + i + 1, sizeof(char*) * count);
            memmove(pollfds + i + 1, devices + i + 2, sizeof(struct pollfd) * count);
            device_count--;
            return 0;
        }
    }
    return -1;
}

static int scan_devices() {
    DIR* dir;
    struct dirent* dent;
    dir = opendir(DEVICE_PATH);
    if (dir == NULL) {
        fprintf(stderr, "could not open directory %s, %s", DEVICE_PATH, strerror(errno));
        return -1;
    }
    while ((dent = readdir(dir))) {
        if (dent->d_name[0] == '.' &&
            (dent->d_name[1] == '\0' || (dent->d_name[1] == '.' && dent->d_name[2] == '\0'))) {
            continue;
        }
        if (open_device(dent->d_name) < 0) {
            return -1;
        }
    }
    closedir(dir);
    return 0;
}

static int update_devices() {
    int event_pos;
    char event_buf[512];
    struct inotify_event* event;

    size_t event_size = read(pollfds[0].fd, event_buf, sizeof(event_buf));
    if (event_size < sizeof(*event)) {
        if (errno == EINTR) return 0;
        fprintf(stderr, "could not get event, %s\n", strerror(errno));
        return 1;
    }

    while (event_pos < event_size) {
        event = (struct inotify_event*)(event_buf + event_pos);
        if (event->len) {
            if (event->mask & IN_CREATE) {
                if (open_device(event->name) < 0) {
                    return -1;
                }
            } else {
                if (close_device(event->name) < 0) {
                    return -1;
                }
            }
        }
        event_pos += sizeof(struct inotify_event) + event->len;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    struct input_event event;
    const char* out_file;
    char selective = argc > 2;
    long long int this_time = 0, last_time = 0;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    out_file = argv[argc - 1];
    out = fopen(out_file, "w");
    if (out == NULL) {
        fprintf(stderr, "failed to open output file %s, %s\n", out_file, strerror(errno));
        return 1;
    }

    pollfds = calloc(1, sizeof(struct pollfd*));
    pollfds[0].fd = inotify_init();
    pollfds[0].events = POLLIN;

    if (inotify_add_watch(pollfds[0].fd, DEVICE_PATH, IN_DELETE | IN_CREATE) < 0) {
        fprintf(stderr, "could not add watch for %s, %s\n", DEVICE_PATH, strerror(errno));
        return 1;
    }
    if (!selective && scan_devices() < 0) {
        fprintf(stderr, "scan devices failed for %s\n", DEVICE_PATH);
        return 1;
    } else if (selective) {
        for (int i = 1; i < argc - 1; i++) {
            if (open_device(argv[i]) < 0) {
                return 1;
            }
        }
    }

    while (1) {
        poll(pollfds, device_count + 1, -1);
        if (pollfds[0].revents & POLLIN) {
            update_devices();
        }
        for (int i = 1; i < device_count + 1; i++) {
            if (pollfds[i].revents & POLLIN) {
                if (read(pollfds[i].fd, &event, sizeof(event)) < sizeof(event)) {
                    fprintf(stderr, "could not get event\n");
                    return 1;
                }
                this_time = event.time.tv_sec * 1E6 + event.time.tv_usec;
                if (last_time == 0) {
                    last_time = this_time;
                }
                fprintf(out, "%14lld ", this_time - last_time);
                fprintf(out, "%s ", devices[i - 1]);
                fprintf(out, "%d %d %d", event.type, event.code, event.value);
                fprintf(out, "\n");
                fflush(out);
                last_time = this_time;
            }
        }
    }

    return 0;
}