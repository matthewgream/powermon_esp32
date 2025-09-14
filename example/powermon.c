
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// ------------------------------------------------------------------------------------------------------------------------

#define SERIAL_BUFFER_SIZE 2048
#define LINE_BUFFER_SIZE   512

// ------------------------------------------------------------------------------------------------------------------------

static bool g_verbose = false;

static bool parse_config(const char *file) {

    FILE *fp = fopen(file, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot open config file '%s' (%s)\n", file, strerror(errno));
        return false;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {

        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *nl, *eq;
        if ((nl = strchr(line, '\n')) != NULL)
            *nl = '\0';
        if ((eq = strchr(line, '=')) != NULL)
            *eq = '\0';
        if (!eq)
            continue;

        const char *key = line, *value = eq + 1;

        //

        if (strcmp(key, "verbose") == 0)
            g_verbose = (strcmp(value, "true") == 0);
    }

    fclose(fp);
    return true;
}

// ------------------------------------------------------------------------------------------------------------------------

static bool serial_check(const char *device) { return access(device, F_OK) == 0; }

static int serial_open(const char *device) {

    const int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0)
        return -1;

    struct termios options;

    if (tcgetattr(fd, &options) < 0)
        goto error;

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag &= (tcflag_t)~CSIZE;
    options.c_cflag &= (tcflag_t)~PARENB;
    options.c_cflag |= (tcflag_t)(CLOCAL | CREAD | CS8);
    options.c_cflag &= (tcflag_t)~CSTOPB;

    options.c_iflag |= (tcflag_t)IGNPAR;
    options.c_iflag &= (tcflag_t) ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY | INPCK);
    options.c_oflag &= (tcflag_t)~OPOST;
    options.c_lflag &= (tcflag_t) ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &options) < 0)
        goto error;

    tcflush(fd, TCIOFLUSH);

    return fd;

error:
    close(fd);
    return -1;
}

static bool serial_readline(const int fd, char *line, const size_t line_size) {

    static char serial_buffer[SERIAL_BUFFER_SIZE];
    static ssize_t serial_length = 0;

    const ssize_t serial_available = SERIAL_BUFFER_SIZE - serial_length - 1;
    if (serial_available > 0) {
        const ssize_t serial_read = read(fd, &serial_buffer[serial_length], (size_t)serial_available);
        if (serial_read > 0)
            serial_length += serial_read;
        else if (serial_read < 0 && errno != EAGAIN) {
            if (g_verbose)
                fprintf(stderr, "error: read (%s)\n", strerror(errno));
            // still process
        }
    }

    if (serial_length > 0) {
        ssize_t serial_offset = -1;
        for (int i = 0; i < serial_length && serial_offset < 0; i++)
            if (serial_buffer[i] == '\n')
                serial_offset = i;
        if (serial_offset >= 0) {
            serial_buffer[serial_offset] = '\0';
            if (serial_offset > 0 && serial_buffer[serial_offset - 1] == '\r')
                serial_buffer[serial_offset - 1] = '\0';
            strncpy(line, serial_buffer, line_size);
            const ssize_t serial_remain = serial_length - serial_offset - 1;
            if (serial_remain > 0)
                memmove(serial_buffer, &serial_buffer[serial_offset + 1], (size_t)serial_remain);
            serial_length = serial_remain;
            return true;
        }
    }

    if (serial_length >= SERIAL_BUFFER_SIZE - 1) {
        fprintf(stderr, "error: serial buffer overflow, resetting\n");
        serial_length = 0;
    }

    return false;
}

// ------------------------------------------------------------------------------------------------------------------------

static void process_line_read(const uint64_t timestamp, const uint64_t sequence, const char *data) {

    printf("%" PRIu64 " %" PRIu64 " READ ", timestamp, sequence);

    const char *ptr = data;
    int device      = 0;

    while (ptr && *ptr) {

        float voltage, current, phase;
        char voltage_fault[32], current_fault[32];

        if (sscanf(ptr, "%f,%f,%f,%31[^,],%31[^, ]", &voltage, &current, &phase, voltage_fault, current_fault) != 5)
            break;

        if (device > 0)
            printf(" ");
        printf("[%d] ", device);
        printf(voltage > 900.0 ? "-," : "%.6fV,", voltage);
        printf(current > 90.0 ? "-," : "%.6fA,", current);
        printf(voltage > 900.0 || current > 90.0 ? "-" : "%+04.0f°", phase);
        printf(" (%s,%s)", voltage_fault, current_fault);

        device++;
        if ((ptr = strchr(ptr, ' ')) != NULL)
            ptr++;
    }

    printf("\n");
    fflush(stdout);
}

static void process_line_diag(const uint64_t timestamp, const uint64_t sequence, const char *data) {

    printf("%" PRIu64 " %" PRIu64 " DIAG ", timestamp, sequence);

    const char *ptr = data;
    int device      = 0;

    while (ptr && *ptr) {

        float voltage_offset, current_offset;
        unsigned long voltage_samples, current_samples;
        char voltage_faults[128], current_faults[128];

        if (sscanf(ptr, "%f,%lu,%127[^;];%f,%lu,%127[^; ]", &voltage_offset, &voltage_samples, voltage_faults, &current_offset, &current_samples, current_faults) != 6)
            break;

        if (device > 0)
            printf(" ");
        printf("[%d] ", device);
        printf("%.1f,%lu,%s;", voltage_offset, voltage_samples, voltage_faults);
        printf("%.1f,%lu,%s", current_offset, current_samples, current_faults);

        device++;
        if ((ptr = strchr(ptr, ' ')) != NULL)
            ptr++;
    }

    printf("\n");
    fflush(stdout);
}

static void process_line_rest(const uint64_t timestamp, const uint64_t sequence, const char *type, const char *data) {

    printf("%" PRIu64 " %" PRIu64 " %s", timestamp, sequence, type);

    if (data && *data)
        printf(" %s", data);

    printf("\n");
    fflush(stdout);
}

static void process_line(const char *line) {

    if (strlen(line) == 0)
        return;

    if (line[0] == '#') {
        if (g_verbose) {
            printf("%s\n", line);
            fflush(stdout);
        }
        return;
    }

    static uint64_t received = 0;
    uint64_t timestamp, sequence;
    char type[16];

    received++;

    if (sscanf(line, "%" SCNu64 " %15s %" SCNu64, &timestamp, type, &sequence) != 3) {
        if (received > 1)
            fprintf(stderr, "error: failed to parse line '%s'\n", line);
        return;
    }

    const char *ptr = line;
    for (int i = 0; i < 3 && ptr; i++) {
        ptr = strchr(ptr, ' ');
        if (ptr)
            ptr++;
    }

    if (strcmp(type, "READ") == 0)
        process_line_read(timestamp, sequence, ptr);
    else if (strcmp(type, "DIAG") == 0)
        process_line_diag(timestamp, sequence, ptr);
    else if (strcmp(type, "INIT") == 0 || strcmp(type, "TERM") == 0 || strcmp(type, "FAIL") == 0)
        process_line_rest(timestamp, sequence, type, ptr);
}

// ------------------------------------------------------------------------------------------------------------------------

static volatile bool g_running = true;

static void signal_handler(__attribute__((unused)) int sig) { g_running = false; }

int main(const int argc, const char *argv[]) {

    if (argc != 4 || strcmp(argv[1], "--config") != 0) {
        fprintf(stderr, "usage: %s --config <config_file> <device>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *config_file = argv[2];
    const char *device      = argv[3];

    if (!parse_config(config_file))
        return EXIT_FAILURE;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    const int fd = serial_open(device);
    if (fd < 0) {
        fprintf(stderr, "error: cannot open device '%s' (%s)\n", device, strerror(errno));
        return EXIT_FAILURE;
    }

    fprintf(stderr, "started on '%s' (verbose=%s)\n", device, g_verbose ? "true" : "false");

    while (g_running) {

        char line[LINE_BUFFER_SIZE];
        if (serial_readline(fd, line, sizeof(line)))
            process_line(line);
        else if (!serial_check(device)) {
            fprintf(stderr, "error: device '%s' disconnected\n", device);
            break;
        }
    }

    close(fd);

    fprintf(stderr, "stopped\n");

    return EXIT_SUCCESS;
}

// ------------------------------------------------------------------------------------------------------------------------
