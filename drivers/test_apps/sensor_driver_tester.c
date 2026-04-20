#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdint.h>
#include <poll.h>

#define TOY_SYSFS_TRIGGER "/sys/sensor_spi/trigger"
#define TOY_SYSFS_NOTIFY "/sys/sensor_spi/notify"

#define SIGUSR 10

#if 0
#define MODULE_FILENAME "/dev/k_spi_driver"
void print_raw_data()
{
    int dev;
    uint8_t buff[3];
    dev = open(MODULE_FILENAME, O_RDONLY);
    if (dev < 0) {
        printf("module open error\n");
        exit(1);
    }
#if 0
    if (write(dev, &buff, 1) < 0) {
        printf("write error\n");
        goto err;
    }
#endif

    if (read(dev, &buff, 3) < 0) {
        printf("read error\n");
        close(dev);
        exit(1);
    }

    printf("Raw Data: 0x%02X 0x%02X 0x%02X\n", buff[0], buff[1], buff[2]);

    close(dev);

    return;
}
#endif

int main(int argc, char *argv[])
{
    int dev;
    int notify_fd, rv;
    char buff[100];
    struct pollfd sensor_fd[1];

    printf("PID: %d\n", getpid());

    // print_raw_data()

    if ((notify_fd = open(TOY_SYSFS_NOTIFY, O_RDWR)) < 0) {
        perror("Unable to open notify");
        exit(1);
    }

    sensor_fd[0].fd = notify_fd;
    sensor_fd[0].events = POLLPRI;

    while (1) {
        rv = poll(sensor_fd, 1, 100000000);
        if (rv < 0) {
            printf("poll error\n");
        }
        else if (rv == 0) {
            printf("Timeout occurred!\n");
        }

        if (read(notify_fd, buff, 100) < 0) {
            continue;
        }

        printf("Sensor read: %s", buff);
        lseek(notify_fd, 0, SEEK_SET);

        printf("Temp: %d\n\n", atoi(buff));
        //     the_sensor_info->press = 11;
        //     the_sensor_info->humidity = 80;
    }

    close(notify_fd);

    return 0;
}
