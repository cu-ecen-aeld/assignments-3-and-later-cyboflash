#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define WRITER_ERR ((int)1)
#define IS_EMPTY(s) ((s) == NULL || (s)[0] == '\0')

int main(int argc, char** argv) {

    openlog("MyWriterApp", LOG_PID | LOG_CONS, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments. Input must be %s <writefile> <writestring>", argv[0]);
        closelog();
        return WRITER_ERR;
    }

    char* writefile = argv[1];
    char* writestring = argv[2];

    if (IS_EMPTY(writefile) || IS_EMPTY(writestring)) {
        syslog(LOG_ERR, "<writefile> or <writestring> or both are empty");
        closelog();
        return WRITER_ERR;
    }

    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        int err_code = errno;
        syslog(LOG_ERR, "Failed to open %s. Error code: %d: %s", writefile, err_code, strerror(err_code));
        closelog();
        return WRITER_ERR;
    }

    if (write(fd, writestring, strlen(writestring)) < 0) {
        int err_code = errno;
        syslog(LOG_ERR, "Unable to write to %s. Error code: %d: %s", writefile, err_code, strerror(err_code));
        closelog();
        return WRITER_ERR;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", writestring, writefile);

    if (close(fd) < 0 ) {
        int err_code = errno;
        syslog(LOG_ERR, "Unable close %s. Error code: %d: %s", writefile, err_code, strerror(err_code));
    }

    closelog();
    return 0;
}
