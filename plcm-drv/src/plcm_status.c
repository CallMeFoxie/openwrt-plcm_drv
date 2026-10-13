#include <arpa/inet.h>
#include <ctype.h>
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
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "plcm_ioctl.h"

#define DISPLAY_WIDTH 20
#define SLOT_COUNT 4
#define MAX_PAGES 32
#define MAX_INTERFACES 16

enum slot {
	LINE1_LEFT,
	LINE1_RIGHT,
	LINE2_LEFT,
	LINE2_RIGHT,
};

struct cpu_sample {
	uint64_t total;
	uint64_t idle;
};

struct page {
	char name[32];
	unsigned int duration;
	char widget[SLOT_COUNT][16];
	char argument[SLOT_COUNT][128];
};

struct interface_sample {
	char name[IFNAMSIZ];
	uint64_t previous_rx;
	uint64_t previous_tx;
	double rx_rate;
	double tx_rate;
	struct timespec previous_time;
	bool initialized;
};

struct configuration {
	char device[128];
	unsigned int refresh;
	struct page pages[MAX_PAGES];
	size_t page_count;
	struct interface_sample interfaces[MAX_INTERFACES];
	size_t interface_count;
};

struct status_data {
	struct cpu_sample previous_cpu;
	unsigned int cpu_percent;
	unsigned int memory_percent;
	double load;
	long uptime;
	bool cpu_initialized;
};

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal_number)
{
	(void)signal_number;
	running = 0;
}

static void copy_string(char *destination, size_t size, const char *source)
{
	if (!source)
		source = "";
	snprintf(destination, size, "%s", source);
}

static unsigned int parse_uint(const char *value, unsigned int fallback)
{
	char *end;
	unsigned long number;

	if (!value || !*value)
		return fallback;
	errno = 0;
	number = strtoul(value, &end, 10);
	if (errno || *end || !number || number > 86400)
		return fallback;
	return (unsigned int)number;
}

static bool widget_enabled(const char *widget)
{
	return widget[0] && strcmp(widget, "none");
}

static struct interface_sample *find_interface(struct configuration *config,
						const char *name)
{
	size_t index;

	for (index = 0; index < config->interface_count; index++) {
		if (!strcmp(config->interfaces[index].name, name))
			return &config->interfaces[index];
	}
	return NULL;
}

static void add_interface(struct configuration *config, const char *name)
{
	struct interface_sample *sample;

	if (!name || !*name || strlen(name) >= IFNAMSIZ ||
	    find_interface(config, name) ||
	    config->interface_count >= MAX_INTERFACES)
		return;
	sample = &config->interfaces[config->interface_count++];
	memset(sample, 0, sizeof(*sample));
	copy_string(sample->name, sizeof(sample->name), name);
}

static void add_empty_page(struct configuration *config)
{
	struct page *page = &config->pages[0];

	memset(page, 0, sizeof(*page));
	copy_string(page->name, sizeof(page->name), "Configuration");
	page->duration = 5;
	copy_string(page->widget[LINE1_LEFT], sizeof(page->widget[0]), "text");
	copy_string(page->argument[LINE1_LEFT], sizeof(page->argument[0]),
		    "No enabled pages");
	copy_string(page->widget[LINE2_LEFT], sizeof(page->widget[0]), "text");
	copy_string(page->argument[LINE2_LEFT], sizeof(page->argument[0]),
		    "Check configuration");
	config->page_count = 1;
}

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [-d device] [-r refresh] "
		"[--page name duration widget argument ...]\n", program);
}

static int parse_configuration(struct configuration *config, int argc, char **argv)
{
	int index = 1;

	memset(config, 0, sizeof(*config));
	copy_string(config->device, sizeof(config->device), "/dev/plcm_drv");
	config->refresh = 1;
	while (index < argc) {
		if (!strcmp(argv[index], "-d") && index + 1 < argc) {
			copy_string(config->device, sizeof(config->device), argv[index + 1]);
			index += 2;
			continue;
		}
		if (!strcmp(argv[index], "-r") && index + 1 < argc) {
			config->refresh = parse_uint(argv[index + 1], config->refresh);
			index += 2;
			continue;
		}
		if (!strcmp(argv[index], "--page") && index + 10 < argc) {
			struct page *page;
			size_t slot;

			if (config->page_count >= MAX_PAGES) {
				index += 11;
				continue;
			}
			page = &config->pages[config->page_count++];
			memset(page, 0, sizeof(*page));
			copy_string(page->name, sizeof(page->name), argv[index + 1]);
			page->duration = parse_uint(argv[index + 2], 5);
			for (slot = 0; slot < SLOT_COUNT; slot++) {
				copy_string(page->widget[slot], sizeof(page->widget[slot]),
					    argv[index + 3 + slot * 2]);
				copy_string(page->argument[slot], sizeof(page->argument[slot]),
					    argv[index + 4 + slot * 2]);
			}
			for (slot = 0; slot < SLOT_COUNT; slot++) {
			if (!strcmp(page->widget[slot], "rx") ||
			    !strcmp(page->widget[slot], "tx"))
				add_interface(config, page->argument[slot]);
			}
			index += 11;
			continue;
		}
		if (!strcmp(argv[index], "-h") || !strcmp(argv[index], "--help")) {
			usage(argv[0]);
			return 1;
		}
		usage(argv[0]);
		return -1;
	}
	if (!config->page_count)
		add_empty_page(config);
	return 0;
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

static int read_load(double *load)
{
	FILE *file = fopen("/proc/loadavg", "r");

	if (!file)
		return -1;
	if (fscanf(file, "%lf", load) != 1) {
		fclose(file);
		errno = EIO;
		return -1;
	}
	fclose(file);
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

	if (!interface || !*interface) {
		errno = EINVAL;
		return -1;
	}
	socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_fd < 0)
		return -1;
	memset(&request, 0, sizeof(request));
	if (strlen(interface) >= sizeof(request.ifr_name)) {
		close(socket_fd);
		errno = ENAMETOOLONG;
		return -1;
	}
	copy_string(request.ifr_name, sizeof(request.ifr_name), interface);
	if (ioctl(socket_fd, SIOCGIFADDR, &request) < 0) {
		close(socket_fd);
		return -1;
	}
	close(socket_fd);
	socket_address = (struct sockaddr_in *)&request.ifr_addr;
	return inet_ntop(AF_INET, &socket_address->sin_addr, address, size) ? 0 : -1;
}

static double elapsed_seconds(const struct timespec *start,
			      const struct timespec *end)
{
	return end->tv_sec - start->tv_sec +
	       (end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void sample_interface(struct interface_sample *sample)
{
	struct timespec now;
	uint64_t rx, tx;
	double elapsed;

	if (read_counter(sample->name, "rx_bytes", &rx) < 0 ||
	    read_counter(sample->name, "tx_bytes", &tx) < 0 ||
	    clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return;
	if (sample->initialized) {
		elapsed = elapsed_seconds(&sample->previous_time, &now);
		if (elapsed > 0.0) {
			sample->rx_rate = rx >= sample->previous_rx ?
				(rx - sample->previous_rx) / elapsed : 0.0;
			sample->tx_rate = tx >= sample->previous_tx ?
				(tx - sample->previous_tx) / elapsed : 0.0;
		}
	}
	sample->previous_rx = rx;
	sample->previous_tx = tx;
	sample->previous_time = now;
	sample->initialized = true;
}

static void update_status(struct configuration *config, struct status_data *status)
{
	struct cpu_sample current;
	struct sysinfo info;
	uint64_t total_delta, idle_delta;
	size_t index;

	if (read_cpu(&current) == 0) {
		if (status->cpu_initialized && current.total >= status->previous_cpu.total &&
		    current.idle >= status->previous_cpu.idle) {
			total_delta = current.total - status->previous_cpu.total;
			idle_delta = current.idle - status->previous_cpu.idle;
			if (total_delta && idle_delta <= total_delta)
				status->cpu_percent = (unsigned int)
					(((total_delta - idle_delta) * 100 + total_delta / 2) /
					 total_delta);
		}
		status->previous_cpu = current;
		status->cpu_initialized = true;
	}
	read_memory_percent(&status->memory_percent);
	read_load(&status->load);
	if (sysinfo(&info) == 0)
		status->uptime = info.uptime;
	for (index = 0; index < config->interface_count; index++)
		sample_interface(&config->interfaces[index]);
}

static void format_rate(char *buffer, size_t size, double rate)
{
	static const char units[] = "BKMGT";
	unsigned int unit = 0;

	while (rate >= 1000.0 && unit < sizeof(units) - 2) {
		rate /= 1000.0;
		unit++;
	}
	if (rate >= 100.0 || unit == 0)
		snprintf(buffer, size, "%.0f%c/s", rate, units[unit]);
	else
		snprintf(buffer, size, "%.1f%c/s", rate, units[unit]);
}

static void format_uptime(char *buffer, size_t size, long uptime)
{
	long days = uptime / 86400;
	long hours = (uptime % 86400) / 3600;
	long minutes = (uptime % 3600) / 60;

	if (days)
		snprintf(buffer, size, "Up %ldd %02ld:%02ld", days, hours, minutes);
	else
		snprintf(buffer, size, "Up %02ld:%02ld", hours, minutes);
}

static void render_widget(struct configuration *config,
			  const struct status_data *status, const char *widget,
			  const char *argument, char *buffer, size_t size)
{
	struct interface_sample *interface;
	char hostname[64];
	struct tm local_time;
	time_t now;

	buffer[0] = '\0';
	if (!strcmp(widget, "text")) {
		copy_string(buffer, size, argument);
	} else if (!strcmp(widget, "hostname")) {
		if (gethostname(hostname, sizeof(hostname)) == 0) {
			hostname[sizeof(hostname) - 1] = '\0';
			copy_string(buffer, size, hostname);
		}
	} else if (!strcmp(widget, "clock") || !strcmp(widget, "date")) {
		now = time(NULL);
		if (localtime_r(&now, &local_time))
			strftime(buffer, size, *argument ? argument :
				 (!strcmp(widget, "clock") ? "%H:%M:%S" : "%Y-%m-%d"),
				 &local_time);
	} else if (!strcmp(widget, "uptime")) {
		format_uptime(buffer, size, status->uptime);
	} else if (!strcmp(widget, "cpu")) {
		snprintf(buffer, size, "CPU %u%%", status->cpu_percent);
	} else if (!strcmp(widget, "ram")) {
		snprintf(buffer, size, "RAM %u%%", status->memory_percent);
	} else if (!strcmp(widget, "load")) {
		snprintf(buffer, size, "Load %.2f", status->load);
	} else if (!strcmp(widget, "ipv4")) {
		if (get_ipv4_address(argument, buffer, size) < 0)
			copy_string(buffer, size, "No IPv4");
	} else if (!strcmp(widget, "rx") || !strcmp(widget, "tx")) {
		char rate[24];

		interface = find_interface(config, argument);
		if (interface) {
			format_rate(rate, sizeof(rate), !strcmp(widget, "rx") ?
				    interface->rx_rate : interface->tx_rate);
			snprintf(buffer, size, "%s %s",
				 !strcmp(widget, "rx") ? "RX" : "TX", rate);
		} else {
			snprintf(buffer, size, "%s n/a",
				 !strcmp(widget, "rx") ? "RX" : "TX");
		}
	}
}

static void format_field(char *line, size_t offset, size_t width,
			 const char *value, bool align_right)
{
	size_t length = strlen(value);
	size_t index;

	if (length > width)
		length = width;
	if (align_right)
		offset += width - length;
	for (index = 0; index < length; index++) {
		unsigned char character = (unsigned char)value[index];
		line[offset + index] = isprint(character) ? (char)character : ' ';
	}
}

static void render_line(struct configuration *config,
			const struct status_data *status, const struct page *page,
			enum slot left, enum slot right, char output[DISPLAY_WIDTH])
{
	char left_value[128], right_value[128];
	bool has_left = widget_enabled(page->widget[left]);
	bool has_right = widget_enabled(page->widget[right]);

	memset(output, ' ', DISPLAY_WIDTH);
	if (has_left)
		render_widget(config, status, page->widget[left], page->argument[left],
			      left_value, sizeof(left_value));
	if (has_right)
		render_widget(config, status, page->widget[right], page->argument[right],
			      right_value, sizeof(right_value));
	if (has_left && has_right) {
		format_field(output, 0, DISPLAY_WIDTH / 2, left_value, false);
		format_field(output, DISPLAY_WIDTH / 2, DISPLAY_WIDTH / 2,
			     right_value, true);
	} else if (has_left) {
		format_field(output, 0, DISPLAY_WIDTH, left_value, false);
	} else if (has_right) {
		format_field(output, 0, DISPLAY_WIDTH, right_value, true);
	}
}

static int write_line(int device_fd, unsigned long line, const char *text)
{
	if (ioctl(device_fd, PLCM_IOCTL_SET_LINE, line) < 0)
		return -1;
	if (write(device_fd, text, DISPLAY_WIDTH) != (ssize_t)DISPLAY_WIDTH)
		return -1;
	return 0;
}

static int render_page(int device_fd, struct configuration *config,
		       const struct status_data *status, const struct page *page)
{
	char line[DISPLAY_WIDTH];

	render_line(config, status, page, LINE1_LEFT, LINE1_RIGHT, line);
	if (write_line(device_fd, 1, line) < 0)
		return -1;
	render_line(config, status, page, LINE2_LEFT, LINE2_RIGHT, line);
	return write_line(device_fd, 2, line);
}

int main(int argc, char **argv)
{
	struct configuration config;
	struct status_data status = { 0 };
	struct timespec sleep_time;
	unsigned int page_elapsed = 0;
	size_t current_page = 0;
	int device_fd;
	int parse_result;

	parse_result = parse_configuration(&config, argc, argv);
	if (parse_result != 0)
		return parse_result > 0 ? 0 : 2;
	device_fd = open(config.device, O_WRONLY | O_CLOEXEC);
	if (device_fd < 0) {
		perror(config.device);
		return 1;
	}
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	update_status(&config, &status);

	while (running) {
		if (render_page(device_fd, &config, &status,
				&config.pages[current_page]) < 0) {
			perror("write display");
			break;
		}
		sleep_time.tv_sec = config.refresh;
		sleep_time.tv_nsec = 0;
		while (nanosleep(&sleep_time, &sleep_time) < 0 && errno == EINTR && running)
			;
		if (!running)
			break;
		update_status(&config, &status);
		page_elapsed += config.refresh;
		if (page_elapsed >= config.pages[current_page].duration) {
			current_page = (current_page + 1) % config.page_count;
			page_elapsed = 0;
		}
	}
	close(device_fd);
	return 0;
}