#include "common.h"
#include "net_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

/* Loop condition */
bool sdrrc_running = true;

/* Callbacks prototypes and typedef */
typedef void (*callback_t)(void *context, int argc, char **argv);
void ignore_cmd_cb(void *magic, int argc, char **argv);
void send_status_cb(void *magic, int argc, char **argv);
void set_mod_cb(void *magic, int argc, char **argv);
void set_freq_cb(void *magic, int argc, char **argv);
void start_cb(void *magic, int argc, char **argv);
void stop_cb(void *magic, int argc, char **argv);
void reload_cb(void *magic, int argc, char **argv);

/* Thread functions prototypes */
void *sta_thread(void *arg);

/* Table of commands and callbacks */
struct mapping_table {
	char *cmd;
	int argc;
	callback_t func;
} cmd_table[] = {
	{"status",  0, &send_status_cb},
	{"start",   0, &start_cb},
	{"stop",    0, &stop_cb},
	{"reload",  0, &reload_cb},
//	{"logout",  0, &ignore_cmd_cb},
//	{"getmod",  0, &ignore_cmd_cb},
	{"setmod",  1, &set_mod_cb},
//	{"getfreq", 0, &ignore_cmd_cb},
	{"setfreq", 1, &set_freq_cb},
	/* Do not remove, keep it as the last one */
	{NULL, 0, NULL}
};

void ignore_cmd_cb(void *magic, int argc, char **argv)
{
	return;
}

void reload_cb(void *magic, int argc, char **argv)
{
	stop_cb(magic, argc, argv);
	start_cb(magic, argc, argv);
}

void stop_cb(void *magic, int argc, char **argv)
{
	struct app_config *cfg = (struct app_config *)magic;
	char buf[STATION_BUFSZ];
	if (!cfg) {
		errno = EFAULT;
		return;
	}

	if (!cfg->child_running) {
		print_info("librtlsdr is not running\n");
		snprintf(buf, STATION_BUFSZ, "<librtlsdr is not running>\n");
		send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);
		return;
	}

	print_info("Stopping librtlsdr...\n");
	snprintf(buf, STATION_BUFSZ, "<Stopping librtlsdr...>\n");
	send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);

	/* Kill the process */
	kill(cfg->rtlsdr_pid, SIGKILL);
	kill(cfg->ffmpeg_pid, SIGKILL);
	cfg->child_running = false;
}

void start_cb(void *magic, int argc, char **argv)
{
	struct app_config *cfg = (struct app_config *)magic;

	int null_fd;
	int stdout_fd = dup(STDOUT_FILENO);
	int stderr_fd = dup(STDERR_FILENO);

	char freq_str[48];
	char buf[STATION_BUFSZ];
	if (!cfg) {
		errno = EFAULT;
		return;
	}

	if (cfg->child_running) {
		print_warn("librtlsdr is already running\n");
		snprintf(buf, STATION_BUFSZ, "<Already running...>\n");
		send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);
		return;
	}

	/* Create a pipe */
	if (pipe(cfg->pfd) < 0) {
		print_error("cannot create a pipe\n");
		exit(7);
	}

	/*
	 * FFmpeg
	 */
	cfg->ffmpeg_pid = fork();
	switch (cfg->ffmpeg_pid) {
	case -1:
		print_error("fork() has failed\n");
		exit(2);
		break;
	case 0:
		/* Close the pipe's writing end */
		close(cfg->pfd[WR_END]);

		/* Attach child's stdin to read from the pipe. */
		dup2(cfg->pfd[RD_END], STDIN_FILENO);

		null_fd = open("/dev/null", O_WRONLY);
		dup2(null_fd, STDOUT_FILENO); /* Ignore any message to stdout */
		dup2(null_fd, STDERR_FILENO); /* Ignore any message to stderr */

		/* Execute ffmpeg */
		execlp("ffmpeg", "ffmpeg", "-re",
			"-f", "s16le", "-ar", "22050", "-i", "pipe:0",
			"-content_type", "audio/ogg", "-f", "ogg",
			"icecast://source:elo330@localhost:8000/stream.ogg",
			NULL);

		/* The following lines will not be executed after a successful
	 	 * invocation of exec() */
		dup2(stdout_fd, STDOUT_FILENO); /* Restore stdout */
		dup2(stderr_fd, STDERR_FILENO); /* Restore stderr */
		print_error("exec() has failed\n");
		exit(3);
		break;
	}

	/*
	 * RTL SDR
	 */
	print_info("Starting librtlsdr...\n");
	snprintf(buf, STATION_BUFSZ, "<Starting librtlsdr...>\n");
	send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);

	cfg->rtlsdr_pid = fork();
	switch (cfg->rtlsdr_pid) {
	case -1:
		cfg->child_running = false;
		break;
	case 0:
		snprintf(freq_str, sizeof freq_str, "%u", cfg->sdr->frequency);
		/* Close the pipe's reading end */
		close(cfg->pfd[RD_END]);

		null_fd = open("/dev/null", O_WRONLY);
		dup2(null_fd, STDERR_FILENO); /* Ignore any message to stderr */

		/* Attach child's stdout to write the pipe. */
		dup2(cfg->pfd[WR_END], STDOUT_FILENO);

		execlp("rtl_fm", "rtl_fm",
			"-M", mcode_to_string(cfg->sdr->modulation),
			"-f", freq_str, "-s", "172000", "-r", "22050",
			"-A", "lut", "-E", "dc", "-", NULL);

		dup2(stdout_fd, STDOUT_FILENO); /* Restore stdout */
		dup2(stderr_fd, STDERR_FILENO); /* Restore stderr */
		print_error("exec() has failed\n");
		exit(-1);
		break;
	default:
		cfg->child_running = true;
	}
}

void send_status_cb(void *magic, int argc, char **argv)
{
	struct app_config *cfg = (struct app_config *)magic;
	char buf[STATION_BUFSZ];
	if (!cfg) {
		errno = EFAULT;
		return;
	}

	print_info("Sending status...\n");
	snprintf(buf, STATION_BUFSZ, "<Freq: %u, Mod: %s, Running: %s>\n",
		cfg->sdr->frequency,
		mcode_to_string(cfg->sdr->modulation),
		(cfg->child_running) ? "yes" : "no");
	send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);
}

void set_mod_cb(void *magic, int argc, char **argv)
{
	struct app_config *cfg = (struct app_config *)magic;
	uint8_t mcode;
	char *mcode_str;
	char buf[STATION_BUFSZ];
	if (!cfg || !argv || !argv[0]) {
		errno = EFAULT;
		return;
	}

	mcode_str = argv[0];
	mcode = string_to_mcode(mcode_str);
	if (mcode != MOD_UNKNOWN) {
		cfg->sdr->modulation = mcode;
		print_info("Changing modulation scheme to %s\n", mcode_str);
		snprintf(buf, STATION_BUFSZ, "<Mod: %s>\n",
			mcode_to_string(cfg->sdr->modulation));
		send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);
	} else {
		print_error("Unknown modulation scheme: %s\n", mcode_str);
	}
}

void set_freq_cb(void *magic, int argc, char **argv)
{
	struct app_config *cfg = (struct app_config *)magic;
	long new_freq;
	char *new_freq_str, *end;
	char buf[STATION_BUFSZ];
	if (!cfg || !argv || !argv[0]) {
		errno = EFAULT;
		return;
	}

	new_freq_str = argv[0];
	new_freq = strtol(new_freq_str, &end, 10);
	if (*end != '\0' || errno == ERANGE) {
		print_error("Invalid frequency value.\n");
		return;
	}
	if (new_freq < 40000 || new_freq > 120000000) {
		print_error("Port is out of range (40000-120000000).\n");
		return;
	}

	cfg->sdr->frequency = new_freq;
	print_info("Changing frequency to %s\n", new_freq_str);
	snprintf(buf, STATION_BUFSZ, "<Freq: %u>\n", cfg->sdr->frequency);
	send(cfg->manager_sock, buf, strnlen(buf, STATION_BUFSZ), MSG_NOSIGNAL);
}

static struct app_config *cfg_alloc_init(void)
{
	struct app_config *cfg = malloc(sizeof *cfg);
	if (!cfg)
		return NULL;

	/* Default options */
	cfg->status = S_IDLE;
	cfg->op_mode = M_STATION;
	cfg->host = calloc(HOST_LEN, sizeof *cfg->host);
	if (!cfg->host)
		goto _err_alloc_host;

	cfg->port = 17920; /* Default */
	cfg->child_running = false;
#if 0
	cfg->need_refresh = true;
	cfg->last_refresh = get_timestamp_ms();
#endif
	cfg->sdr = malloc(sizeof *cfg->sdr);
	if (!cfg->sdr)
		goto _err_alloc_sdr;

	/* Sane defaults */
	cfg->sdr->modulation = MOD_FM;   /* FM */
	cfg->sdr->frequency = 94500000; /* Radio Bio Bio */

	return cfg;

_err_alloc_sdr:
	free(cfg->host);
_err_alloc_host:
	return NULL;
}

static void cfg_free(struct app_config *cfg)
{
	if (!cfg)
		return;
	if (cfg->host)
		free(cfg->host);
	if (cfg->sdr)
		free(cfg->sdr);

	free(cfg);
}

/* Options from command line */
static struct option opts[] = {
	{"manager", no_argument,       NULL, 'm'},
	{"host",    required_argument, NULL, 'h'},
	{"port",    required_argument, NULL, 'p'},
	{NULL,      0,                 NULL, 0}
};

static void parse_args(int argc, char *const *argv, struct app_config *cfg)
{
	int c;
	int port;
	char *end;

	uid_t uid = getuid();
	uid_t euid = geteuid();
	bool host_is_set = false;

	if (!cfg)
		return;

	/* Argument parsing */
	while ((c = getopt_long(argc, argv, "mh:p:", opts, NULL)) != -1) {
		switch (c) {
		case 'm':
			cfg->op_mode = M_MANAGER;
			break;
		case 'h':
			snprintf(cfg->host, HOST_LEN, "%s", optarg);
			host_is_set = true;
			break;
		case 'p':
			port = strtol(optarg, &end, 10);
			if (*end != '\0' || errno == ERANGE) {
				print_error("Invalid port value.\n");
				goto _parse_abort;
			}
			if (port < 1 || port > 65535) {
				print_error("Port is out of range (1-65535).\n");
				goto _parse_abort;
			}
			cfg->port = port;
			break;
		case '?':
			/* Simply ignore invalid options and continue */
			break;
		}
	}

	if (cfg->op_mode == M_MANAGER) {
		if (cfg->port == 0 || !host_is_set) {
			print_error("Missing host and/or port.\n");
			goto _parse_abort;
		}
	} else {
		if (cfg->port < 1024 && uid != 0 && euid !=0) {
			print_error("Binding ports below 1024 require root privileges.\n");
			exit(254);
		}
	}

	return;

_parse_abort:
//	print_usage(argv[0]);
	exit(255);
	return;
}

ssize_t sta_recv_messages(struct app_config *cfg, void *buffer, size_t n)
{
	struct mapping_table *ct;
	char *ret = NULL, *saveptr, **argv;
	char *buf;
	ssize_t nbr;

	if (!cfg || !cfg->sdr || !buffer) {
		errno = EFAULT;
		return -1;
	}

	/* Read the current line */
	buf = buffer;
	nbr = read_line(cfg->manager_sock, buf, n);
	if (nbr <= 0)
		return nbr;

	/* Trim trailing spaces */
	strtrim(buffer);

	/* Search for supported commands */
	for (ct = cmd_table; ct->cmd; ct++)
		if ((ret = strstr(buf, ct->cmd)))
			break;

	/* On success, prepare arguments and execute its callback */
	if (ret) {
		if (ct->argc > 0) {
			int i = 0;
			char *tmp;

			argv = malloc(ct->argc * sizeof **argv);
			if (!argv) {
				print_error("No enough memory for callback's args\n");
				errno = ENOMEM;
				goto _abort;
			}

			tmp = strtok_r(ret, " ", &saveptr);
			while (tmp != NULL && i < ct->argc) {
				tmp = strtok_r(NULL, " ", &saveptr);
				argv[i++] = tmp;
			}

			if (i != ct->argc || !argv[i-1]) { /* Need a better condition */
				print_error("<%s>: missing arguments\n", ct->cmd);
				free(argv);
				goto _abort;
			}

			ct->func(cfg, ct->argc, argv);
			free(argv);
		} else {
			ct->func(cfg, 0, NULL);
		}
	}

_abort:
	return nbr;
}

void *sta_thread(void *arg)
{
	struct app_config *cfg = arg;
	char buf[STATION_BUFSZ];
	int maxfd, retval;
	fd_set master_fds, working_fds;

	if (!cfg)
		return NULL;

	FD_ZERO(&master_fds);
	FD_SET(cfg->manager_sock, &master_fds);
	maxfd = 1 + cfg->manager_sock;

	while (cfg->status == S_ESTABLISHED) {
		working_fds = master_fds;
		retval = select(maxfd, &working_fds, NULL, NULL, NULL);
		if (retval <= 0)
			continue;

		if (FD_ISSET(cfg->manager_sock, &working_fds)) {
			if (sta_recv_messages(cfg, buf, STATION_BUFSZ) <= 0)
				break;
#if 0
			if ((nbw = send(sockets[1], buf, nbr, MSG_NOSIGNAL)) <= 0)
				break;
#endif
		}
	}

	cfg->status = S_FINISHED;
	return NULL;
}

int sta_mode_loop(struct app_config *cfg)
{
	int listen_sock;
	listen_sock = tcp_server_socket(cfg->port, LISTEN_BACKLOG);
	fd_set master_fds, working_fds;

	FD_ZERO(&master_fds);
	FD_SET(listen_sock, &master_fds);

	while (sdrrc_running) {
		int new_sock;
		int retval, maxfd;
		struct timeval sel_timeout = {.tv_sec = 0, .tv_usec = 1000};
		struct sockaddr_in addr;
		socklen_t len = sizeof addr;

		/* Check if there is a new connection. */
		working_fds = master_fds;
		maxfd = 1 + listen_sock;
		retval = select(maxfd, &working_fds, NULL, NULL, &sel_timeout);
		if (retval < 0)
			continue;

		switch (cfg->status) {
		case S_LISTENING:
			/* Accept the new connection */
			if (FD_ISSET(listen_sock, &working_fds)) {
				pthread_t tid;
				new_sock = accept(listen_sock, (struct sockaddr *)&addr, &len);

				print_info("New connection!\n");

				/* Update status */
				cfg->manager_sock = new_sock;
				cfg->status = S_ESTABLISHED;

				/* Run thread */
				pthread_create(&tid, NULL, &sta_thread, cfg);

				/* When a detached thread terminates, its resources are
			 	 * automatically released. */
				pthread_detach(tid);
			}
			break;
		case S_ESTABLISHED:
			/* Acceppt and drop new incoming connections */
			if (FD_ISSET(listen_sock, &working_fds)) {
				new_sock = accept(listen_sock, (struct sockaddr *)&addr, &len);
				print_warn("Already got a manager. Dropping incoming connection.\n");
				close(new_sock);
			}
			break;
		case S_FINISHED:
			print_info("Closing manager connection\n");
			close(cfg->manager_sock);

			/* Recycling */
			cfg->status = S_LISTENING;
			break;
		}
	}

	return 0;
}

static void handle_signal(int signum)
{
	switch (signum) {
	case SIGINT: /* Falls through */
	case SIGTERM:
		sdrrc_running = false;
		printf("\n");
		print_warn("Closing program...\n");
		break;
	}
}

int main(int argc, char *const *argv)
{
	struct app_config *cfg;
	struct sigaction sa;

	int retval;

	if (!(cfg = cfg_alloc_init())) {
		print_error("Cannot allocate memory\n");
		exit(253);
	}

	/* Parse and pack arguments into a struct */
	parse_args(argc, argv, cfg);

	/* Register signal handler */
	sa.sa_handler = &handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (cfg->op_mode == M_STATION) {
		print_info("Running in station mode, listening on port: tcp/%u\n",
			cfg->port);

		cfg->status = S_LISTENING;
		retval = sta_mode_loop(cfg);
	} else {
		retval = -1;
	}

	cfg_free(cfg);
	return retval;
}