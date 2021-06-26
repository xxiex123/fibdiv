#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
//#include <gmp.h>

#define FIB_DEV "/dev/fibonacci"
#define BUF_SIZE 500
/*
void printBuff(__int128 buf)
{
    if (buf == NULL) {
        printf("0");
        return;
    }

    char output[128];
    int index = 0;

    while (buf != 0) {
        output[index] = "0123456789"[buf % 10];
        buf = buf / 10;
        index++;
    }

    for (int i = index - 1; i >= 0; i--)
        printf("%c", output[i]);
}
*/
int main()
{
    char write_buf[1];
    char read_buf[BUF_SIZE];
    int offset = 1000; /* TODO: try test something bigger than the limit */

    FILE *f_time = fopen("time.txt", "wb+");
    int fd = open(FIB_DEV, O_RDWR);

    if (f_time == NULL) {
        perror("File declaration fail");
        exit(1);
    }
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        //__int128 buf;
        lseek(fd, i, SEEK_SET);
        read(fd, read_buf, BUF_SIZE);
        int time = write(fd, write_buf, strlen(write_buf));
        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        // printBuff(buf);
        printf("%s", read_buf);
        printf(".\n");
        fprintf(f_time, "%d %d\n", i, time);
    }

    /*
    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, buf, BUF_SIZE);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%lld.\n",
               i, sz);
    }
    */

    close(fd);
    fclose(f_time);
    return 0;
}
