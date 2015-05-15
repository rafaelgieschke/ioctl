/* vim: set sw=4 expandtab: */
/*
 * Licence: GPL
 * Created: 2015-05-12 09:53:35+02:00
 * Main authors:
 *     - Jérôme Pouiller <jezz@sysmic.org>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>
#include <error.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>


#define ARRAY_SIZE(A) (sizeof(A) / sizeof(A[0]))
#define IOCTL_ENTRY_ADV(X, DIR, SZ) { X, STRINGIFY(X), -1 , -1 },
#define IOCTL_ENTRY(X) IOCTL_ENTRY_ADV(X, -1, -1)

//static const char *ioctl_list[] = {
//    IOCTL_ENTRY()
//}

static const char *dir_str[] = {
  "NONE", "R", "W", "RW"
};

void usage(FILE *out, int code) {
    fprintf(out,
            "Usage: %s [OPTIONS] FILEDEV IOCTL_NUM\n"
            "Call ioctl IOCTL_NUM on FILEDEV.\n"
            "A buffer is allocated and passed as argument of ioctl. If direction is\n"
            "not 'NONE', buffer content is read/write on standard input/ouput.\n"
            "Direction and buffer size are deduced from IOCTL_NUM. It is however\n"
            "possible to force these parameters.\n"
            "\n"
            "\t-d DIR   force direction: 0 = NONE, 1 = R, 2 = W, 3 = RW\n"
            "\t-s SIZE  force buffer size\n"
            "\t-v VALUE pass this value as ioctl argument instead of a pointer on a buffer. Force direction to NONE\n"
            "\t-q       quiet\n",
            program_invocation_short_name);
    exit(code);
}


void sighandler(int signum, siginfo_t *pinfo, void *context) {
    signum = signum;
    context = context;
    psiginfo(pinfo, "ioctl returned with signal");
    // fprintf(stderr, "ioctl returned with signal %s (%d)\n", strsignal(sig), sig);
}

void doit(const char *file, int ioctl_nr, void *buf) {
    struct sigaction act = { }, oldact;
    int ret;
    int fd;
    int i;

    fd = open(file, O_RDWR);
    if (fd < 0 && errno == EPERM)
        fd = open(file, O_RDONLY);
    if (fd < 0) {
        error(1, errno, "Cannot open %s: ", file);
    }

    memset(&act, 0, sizeof(act));
    act.sa_sigaction = sighandler;
    act.sa_flags = SA_SIGINFO;
    for (i = 0; i < NSIG; i++) {
        sigaction(i, NULL, &oldact);
        if (oldact.sa_handler == SIG_DFL)
            sigaction(i, &act, NULL);
    }
    ret = ioctl(fd, ioctl_nr, buf);
    memset(&act, 0, sizeof(act));
    act.sa_handler = NULL;
    for (i = 0; i < NSIG; i++) {
        sigaction(i, NULL, &oldact);
        if (oldact.sa_sigaction == sighandler)
            sigaction(i, &act, NULL);
    }
    if (ret)
        fprintf(stderr, "Returned %d (errno: %d, \"%m\")\n", ret, errno);
    else
        fprintf(stderr, "Returned 0\n");
}

void display_parms(char *prefix, int ioctl_nr, int dir, int size, void *force_value) {
    int type = _IOC_TYPE(ioctl_nr);
    int nr = _IOC_NR(ioctl_nr);

    fprintf(stderr, "%s: ioctl=0x%08x, ", prefix, ioctl_nr);
    if (force_value == (void *) -1)
        fprintf(stderr, "direction=%s, arg size=%d bytes, ", dir_str[dir], size);
    else
        fprintf(stderr, "arg value=%p, ", force_value);
    fprintf(stderr, "device number=0x%02x", type);
    if (isprint(type))
        fprintf(stderr, " ('%c')", (char) type);
    fprintf(stderr, ", function number=%u\n", nr);
}

int main(int argc, char **argv) {
    int ioctl_nr;
    int dir, force_dir = -1;
    int size, force_size = -1;
    void *buf = NULL, *force_value = (void *) -1;
    const char *file;
    int quiet = 0;
    int opt;
    int i;

    while ((opt = getopt(argc, argv, "d:s:v:qh")) != -1) {
        switch (opt) {
            case 'd':
                errno = 0;
                force_dir = strtol(optarg, NULL, 0);
                if (errno) {
                    force_dir = -1;
                    for (i = 0; i < ARRAY_SIZE(dir_str); i++)
                        if (!strcmp(optarg, dir_str[i]))
                            force_dir = i;
                    if (force_dir == -1 || force_dir > ARRAY_SIZE(dir_str))
                        error(1, 0, "Invalid direction");
                }
                break;
            case 's':
                errno = 0;
                force_size = strtol(optarg, NULL, 0);
                if (errno)
                    usage(stderr, EXIT_FAILURE);
                break;
            case 'v':
                errno = 0;
                force_value = (void *) strtol(optarg, NULL, 0);
                if (errno)
                    usage(stderr, EXIT_FAILURE);
                break;
            case 'q':
                quiet = 1;
                break;
            case 'h':
                usage(stdout, 0);
                break;
            default: /* '?' */
                usage(stderr, EXIT_FAILURE);
                break;
        }
    }
    if (optind + 2 != argc)
        usage(stderr, EXIT_FAILURE);
    file = argv[optind];
    errno = 0;
    ioctl_nr = strtol(argv[optind + 1], NULL, 0);
    dir = _IOC_DIR(ioctl_nr);
    size = _IOC_SIZE(ioctl_nr);
    if (!quiet)
        display_parms("Decoded values", ioctl_nr, dir, size, (void *) -1);
    if (force_value != (void *) -1 && force_size != -1)
        error(1, 0, "Options -v and -s are incompatible");
    if (force_value != (void *) -1 && force_dir != -1)
        error(1, 0, "Options -v and -d are incompatible");
    if (force_dir != -1)
        dir = force_dir;
    if (force_size != -1)
        size = force_size;
    if (force_value != (void *) -1) {
        buf = force_value;
        dir = 0;
        size = 0;
    } else if (!size) {
        buf = NULL;
    } else {
        buf = malloc(size);
        memset(buf, 0, size);
    }
    if (dir == 0 && size != 0)
        fprintf(stderr, "Warning: Direction is NONE but buffer size is not 0\n");
    if (!quiet)
        display_parms("Used values", ioctl_nr, dir, size, force_value);

    if (dir == 1 || dir == 3)
        read(0, buf, size);
    doit(file, ioctl_nr, buf);
    if (dir == 2 || dir == 3)
        write(1, buf, size);

    return 0;
}
