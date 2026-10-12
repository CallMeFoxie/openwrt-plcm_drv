#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "plcm_ioctl.h"

#define DISPLAY_WIDTH 20

struct cpu_sample {
	uint64_t total;
	uint64_t idle;
};

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal_number)
{
	(void)signal_number;
	running = 0;
}

static int read_cpu(struct cpu_sample *sample)
{
	FILE *file;
	char label[8];
	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;

	file = fopen("/proc/stat", "r");
	if (!file)
		return -1;

	if (fscanf(file, "%7s %llu %llu %llu %llu %llu %llu %llu %llu",
		   label, &user, &nice, &system, &idle, &iowait, &irq,
		   &softirq, &steal) != 9) {
		fclose(file);
		errno = EIO;
		return -1;
	}
	fclose(file);

	sample->idle = idle + iowait;
	sample->total = user + nice + system + idle + iowait + irq + softirq + steal;
	return 0;
}

static int read_memory_percent(unsigned int *percent)
{
	FILE *file;
	char line[128];
	unsigned long long total = 0;
	unsigned long long available = 0;

	file = fopen("/proc/meminfo", "r");
	if (!file)
		return -1;

	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "MemTotal: %llu kB", &total) == 1)
			continue;
		if (sscanf(line, "MemAvailable: %llu kB", &available) == 1)
			continue;
	}
	fclose(file);

	if (!total || available > total) {
		errno = EIO;
		return -1;
	}

	*percent = (unsigned int)(((total - available) * 100 + total / 2) / total);
	return 0;
}

static int read_counter(const char *interface, const char *counter,
			uint64_t *value)
{
	char path[256];
	FILE *file;
	unsigned long long raw_value;

	if (snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/%s",
		     interface, counter) >= (int)sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	file = fopen(path, "r");
	if (!file)
		return -1;
	if (fscanf(file, "%llu", &raw_value) != 1) {
		fclose(file);
		errno = EIO;
		return -1;
	}
	fclose(file);
	*value = raw_value;
	return 0;
}

static int get_ipv4_address(const char *interface, char *address, size_t size)
{
	struct sockaddr_in *socket_address;
	struct ifreq request;
	int socket_fd;

	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd < 0)
		return -1;

	memset(&request, 0, sizeof(request));
	if (strlen(interface) >= sizeof(request.ifr_name)) {
		close(socket_fd);
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(request.ifr_name, interface);

	if (ioctl(socket_fd, SIOCGIFADDR, &request) < 0) {
		close(socket_fd);
		return -1;
	}
	close(socket_fd);

	socket_address = (struct sockaddr_in *)&request.ifr_addr;
	if (!inet_ntop(AF_INET, &socket_address->sin_addr, address, size))
		return -1;
	return 0;
}

static void format_rate(char *buffer, size_t size, uint64_t bytes,
			double seconds)
{
	static const char units[] = " KMGT";
	double rate = seconds > 0.0 ? bytes / seconds : 0.0;
	unsigned int unit = 0;

	while (rate >= 1000.0 && unit < sizeof(units) - 2) {
		rate /= 1000.0;
		unit++;
	}

	if (rate >= 100.0 || unit == 0)
		snprintf(buffer, size, "%.0f%c", rate, units[unit]);
	else
		snprintf(buffer, size, "%.1f%c", rate, units[unit]);
}

static int write_line(int device_fd, unsigned long line, const char *text)
{
	char output[DISPLAY_WIDTH];
	size_t length = strlen(text);

	memset(output, ' ', sizeof(output));
	if (length > sizeof(output))
		length = sizeof(output);
	memcpy(output, text, length);

	if (ioctl(device_fd, PLCM_IOCTL_SET_LINE, line) < 0)
		return -1;
	if (write(device_fd, output, sizeof(output)) != (ssize_t)sizeof(output))
		return -1;
	return 0;
}

static double elapsed_seconds(const struct timespec *start,
			      const struct timespec *end)
{
	return end->tv_sec - start->tv_sec +
	       (end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [-d device] [-i interface] [-r refresh_seconds] "
		"[-p page_seconds]\n", program);
}

int main(int argc, char **argv)
{
	const char *device = "/dev/plcm_drv";
	const char *interface = "eth0";
	unsigned int refresh_seconds = 2;
	unsigned int page_seconds = 5;
	struct cpu_sample previous_cpu, current_cpu;
	uint64_t previous_rx, previous_tx, current_rx, current_tx;
	struct timespec previous_time, current_time, sleep_time;
	unsigned int cpu_percent, memory_percent;
	unsigned int page_elapsed = 0;
	bool bandwidth_page = false;
	char line[64], address[INET_ADDRSTRLEN];
	char rx_rate[16], tx_rate[16];
	uint64_t total_delta, idle_delta;
	double seconds;
	int device_fd;
	int option;

	while ((option = getopt(argc, argv, "d:i:r:p:h")) != -1) {
		switch (option) {
		case 'd':
			device = optarg;
			break;
		case 'i':
			interface = optarg;
			break;
		case 'r':
			refresh_seconds = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		case 'p':
			page_seconds = (unsigned int)strtoul(optarg, NULL, 10);
			break;
		default:
			usage(argv[0]);
			return option == 'h' ? 0 : 2;
		}
	}
	if (!refresh_seconds || !page_seconds) {
		usage(argv[0]);
		return 2;
	}

	device_fd = open(device, O_WRONLY | O_CLOEXEC);
	if (device_fd < 0) {
		perror(device);
		return 1;
	}

	if (read_cpu(&previous_cpu) < 0 ||
	    read_counter(interface, "rx_bytes", &previous_rx) < 0 ||
	    read_counter(interface, "tx_bytes", &previous_tx) < 0 ||
	    clock_gettime(CLOCK_MONOTONIC, &previous_time) < 0) {
		perror("initial status sample");
		close(device_fd);
		return 1;
	}

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	sleep_time.tv_sec = refresh_seconds;
	sleep_time.tv_nsec = 0;

	while (running) {
		sleep_time.tv_sec = refresh_seconds;
		sleep_time.tv_nsec = 0;
		while (nanosleep(&sleep_time, &sleep_time) < 0 && errno == EINTR && running)
			;
		if (!running)
			break;

		if (read_cpu(&current_cpu) < 0 ||
		    read_memory_percent(&memory_percent) < 0 ||
		    read_counter(interface, "rx_bytes", &current_rx) < 0 ||
		    read_counter(interface, "tx_bytes", &current_tx) < 0 ||
		    clock_gettime(CLOCK_MONOTONIC, &current_time) < 0) {
			perror("status sample");
			continue;
		}

		total_delta = current_cpu.total - previous_cpu.total;
		idle_delta = current_cpu.idle - previous_cpu.idle;
		if (current_cpu.total < previous_cpu.total || idle_delta > total_delta) {
			total_delta = 0;
			idle_delta = 0;
		}
		cpu_percent = total_delta ?
			(unsigned int)(((total_delta - idle_delta) * 100 + total_delta / 2) /
				       total_delta) : 0;
		snprintf(line, sizeof(line), "CPU %3u%% RAM %3u%%", cpu_percent,
		 memory_percent);
		if (write_line(device_fd, 1, line) < 0) {
			perror("write display");
			break;
		}

		seconds = elapsed_seconds(&previous_time, &current_time);
		if (bandwidth_page) {
			format_rate(rx_rate, sizeof(rx_rate),
				    current_rx >= previous_rx ? current_rx - previous_rx : 0,
				    seconds);
			format_rate(tx_rate, sizeof(tx_rate),
				    current_tx >= previous_tx ? current_tx - previous_tx : 0,
				    seconds);
			snprintf(line, sizeof(line), "RX:%s TX:%s B/s", rx_rate, tx_rate);
		} else if (get_ipv4_address(interface, address, sizeof(address)) == 0) {
			snprintf(line, sizeof(line), "%s %s", interface, address);
		} else {
			snprintf(line, sizeof(line), "%s no IPv4", interface);
		}
		if (write_line(device_fd, 2, line) < 0) {
			perror("write display");
			break;
		}

		previous_cpu = current_cpu;
		previous_rx = current_rx;
		previous_tx = current_tx;
		previous_time = current_time;
		page_elapsed += refresh_seconds;
		if (page_elapsed >= page_seconds) {
			bandwidth_page = !bandwidth_page;
			page_elapsed = 0;
		}
	}

	close(device_fd);
	return 0;
}