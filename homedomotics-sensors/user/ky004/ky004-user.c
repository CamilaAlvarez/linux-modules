#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/select.h>
#include "../include/ky004-data.h"

int main(int argc, char **argv)
{
    int fd, ret;
    fd_set readfs;

    fd = open(KY004_DEVICE, O_RDONLY);
    if (fd < -1)
        return 1;
    FD_ZERO(&readfs);
    FD_SET(fd, &readfs);
    ret = select(fd + 1, &readfs, NULL, NULL, NULL);
    if (ret == -1)
    {
        fprintf(stderr, "select: an error ocurred");
        return 1;
    }
    if (FD_ISSET(fd, &readfs))
    {
        printf("Button was pressed\n");
        return 0;
    }
    fprintf(stderr, "Woke up, but button was not pressed");
    return 1;
}