#ifndef __COMMON_H__
#define __COMMON_H__

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <sys/time.h>

#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#endif

#define RD_END      0
#define WR_END      1
#define HOST_LEN    256

/* Supported modulation schemes */
#define MOD_FM      0x00
#define MOD_WBFM    0x01
#define MOD_RAW     0x02
#define MOD_AM      0x03
#define MOD_USB     0x04
#define MOD_LSB     0x05
#define MOD_UNKNOWN 0xFF

struct sdr_settings {
	uint8_t modulation;
	uint32_t frequency;
};

/* Status */
#define S_IDLE          0x00
#define S_LISTENING     0x01
#define S_ESTABLISHED   0x02
#define S_FINISHED      0x03

/* Running modes */
#define M_STATION       0x00
#define M_MANAGER       0x01

struct app_config {
	uint8_t  status;
	uint8_t  op_mode;

	char    *host;
	uint16_t port;

	int      pfd[2];
	int      manager_sock;
	pid_t    ffmpeg_pid;
	pid_t    rtlsdr_pid;
	bool     child_running;
	struct   sdr_settings *sdr;
};

/* Convert modulation code into string */
static const char *mcode_to_string(uint8_t mcode)
{
	switch (mcode) {
	case MOD_FM:
		return "fm";
	case MOD_WBFM:
		return "wbfm";
	case MOD_RAW:
		return "raw";
	case MOD_AM:
		return "am";
	case MOD_USB:
		return "usb";
	case MOD_LSB:
		return "lsb";
	}
	return NULL;
}

/* Convert string into mcode */
static uint8_t string_to_mcode(const char *str)
{
	struct {
		const char *mcode_str;
		uint8_t mcode;
	} mcode_tbl[] = {
		{"fm",   MOD_FM},
		{"wbfm", MOD_WBFM},
		{"raw",  MOD_RAW},
		{"am",   MOD_AM},
		{"usb",  MOD_USB},
		{"lsb",  MOD_LSB},
		{NULL,   MOD_UNKNOWN}
	}, *mt;

	for (mt = mcode_tbl; mt->mcode_str; mt++) {
		if (strcmp(mt->mcode_str, str) == 0)
			break;
	}

	return mt->mcode;
}

/* Generic timestamp function: Returns UTC epoch time in ms */
static inline uint64_t get_timestamp_ms(void)
{
	uint64_t timestamp;

#if _POSIX_C_SOURCE >= 199309L
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	timestamp = (ts.tv_sec * UINT64_C(1000) + ts.tv_nsec / 1000000);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	timestamp = (tv.tv_sec * UINT64_C(1000) + tv.tv_usec / 1000);
#endif

	return timestamp;
}

/* Trim trailing whitespaces */
static void strtrim(char *str)
{
	int i;
	int begin = 0;
	int end = strlen(str) - 1;

	while (isspace((unsigned char)str[begin]))
		begin++;

	while ((end >= begin) && isspace((unsigned char)str[end]))
		end--;

	for (i = begin; i <= end; i++)
		str[i - begin] = str[i];

	str[i - begin] = '\0';
}

/* Codes for coloring output text */
#define COLOR_OFF       "\x1B[0m"
#define COLOR_RED       "\x1B[0;91m"
#define COLOR_GREEN     "\x1B[0;92m"
#define COLOR_YELLOW    "\x1B[0;93m"
#define COLOR_BLUE      "\x1B[0;94m"
#define COLOR_MAGENTA   "\x1B[0;95m"
#define COLOR_BOLDGRAY  "\x1B[1;30m"
#define COLOR_BOLDWHITE "\x1B[1;37m"

/* ENABLE_DEBUG should be set on compile time */
#define debug_alert(fmt, ...) \
	do { if (ENABLE_DEBUG) fprintf(stderr, COLOR_RED fmt COLOR_OFF, ##__VA_ARGS__); } while (0)
#define debug_warn(fmt, ...) \
	do { if (ENABLE_DEBUG) fprintf(stderr, COLOR_YELLOW fmt COLOR_OFF, ##__VA_ARGS__); } while (0)
#define debug_success(fmt, ...) \
	do { if (ENABLE_DEBUG) fprintf(stderr, COLOR_GREEN fmt COLOR_OFF, ##__VA_ARGS__); } while (0)
#define debug_info(fmt, ...) \
	do { if (ENABLE_DEBUG) fprintf(stderr, COLOR_BOLDWHITE fmt COLOR_OFF, ##__VA_ARGS__); } while (0)
#define print_error(fmt, ...) \
	do { fprintf(stderr, COLOR_RED "ERROR: " fmt COLOR_OFF, ##__VA_ARGS__); } while (0)
#define print_warn(fmt, ...) \
	do { fprintf(stderr, COLOR_YELLOW "WARN: " fmt COLOR_OFF, ##__VA_ARGS__); } while (0)
#define print_info(fmt, ...) \
	do { fprintf(stdout, COLOR_OFF "INFO: " fmt COLOR_OFF, ##__VA_ARGS__); } while (0)

#endif /* __COMMON_H__ */
