#include <stdio.h>
#include <unistd.h> // exit, sleep
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> //O_RDWR
#include <signal.h>
#include <stdlib.h> // malloc, free, y otras
#include <sys/ioctl.h>
#include "../led_lkm.h"

#define DEFAULT_REPETITIONS 10

int main(int argc, char **argv)
{
    int fd, power;

    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s device_file number_repetitions\n\
  If device_file does not exist, create it using mknod(1) (as root)\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    int number_repetitions = atoi(argv[3]);
    if (number_repetitions == 0)
    {
        number_repetitions = DEFAULT_REPETITIONS;
    }

    if ((fd = open(argv[1], O_RDWR, 0)) == -1)
    {
        perror("open");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < number_repetitions; i++)
    {
        if (ioctl(fd, IOCTL_POWER_ON, 0) == -1)
        {
            perror("ioctl IOCTL_POWER_ON failed");
            close(fd);
            exit(EXIT_FAILURE);
        }
        if (ioctl(fd, IOCTL_POWER_READ, &power) == -1)
        {
            perror("ioctl IOCTL_POWER_READ failed");
            close(fd);
            exit(EXIT_FAILURE);
        }
        printf("Power: %d (i=%d)\n", power, i);
        sleep(1);
        if (ioctl(fd, IOCTL_POWER_OFF, 0) == -1)
        {
            perror("ioctl IOCTL_POWER_OFF failed");
            close(fd);
            exit(EXIT_FAILURE);
        }

        if (ioctl(fd, IOCTL_POWER_READ, &power) == -1)
        {
            perror("ioctl IOCTL_POWER_READ failed");
            close(fd);
            exit(EXIT_FAILURE);
        }
        sleep(1);
        printf("Power: %d (i=%d)\n", power, i);
    }

    close(fd);
    exit(EXIT_SUCCESS);
}
