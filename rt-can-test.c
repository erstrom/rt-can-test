/*
 * Copyright (c) 2017 Erik Stromdahl <erik.stromdahl@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <unistd.h>
#include <malloc.h>
#include <errno.h>
#include "can.h"
#include "lib.h"

/* Extra heap size intended to cover all dynamic memory this process
 * might use.
 * If the process allocates more than this limit, there is a risk of
 * page faults.
 */
#define HEAP_TOUCH_SZ (1024 * 1024)

static bool run_tx_test, run_rx_test, verbose;
static struct can_hdl *can_hdl;
static struct canfd_frame *can_tx_frame, *can_rx_frame;
static struct timespec tx_interval;

static void setup_mem(void)
{
	int i, page_size;
	char *buf;

	/* Lock all current and future pages */
	if (mlockall(MCL_CURRENT | MCL_FUTURE))
		printf("mlockall failed\n");

	/* Turn off malloc trimming. */
	mallopt(M_TRIM_THRESHOLD, -1);

	/* Turn off malloc mmap usage. */
	mallopt(M_MMAP_MAX, 0);

	page_size = sysconf(_SC_PAGESIZE);
	buf = malloc(HEAP_TOUCH_SZ);

	/* Touch each memory page in order to make sure we get all page faults
	 * now and not later on during execution
	 */
	for (i = 0; i < HEAP_TOUCH_SZ; i += page_size)
		buf[i] = 0;

	free(buf);
}

static int create_thread(pthread_t *thread,
			 void *thread_func(void *),
			 void *arg)
{
	struct sched_param param;
	void *stack_buf;
	pthread_attr_t attr;
	int ret;

	stack_buf = mmap(NULL, PTHREAD_STACK_MIN, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack_buf == MAP_FAILED) {
		printf("mmap failed\n");
		ret = -1;
		goto out;
	}
	memset(stack_buf, 0, PTHREAD_STACK_MIN);

	/* Initialize pthread attributes (default values) */
	ret = pthread_attr_init(&attr);
	if (ret) {
		printf("init pthread attributes failed\n");
		goto out;
	}

	/* Set pthread stack to already pre-faulted stack */
	ret = pthread_attr_setstack(&attr, stack_buf, PTHREAD_STACK_MIN);
	if (ret) {
		printf("pthread setstack failed\n");
		goto out;
	}

	/* Set scheduler policy and priority of pthread */
	ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
	if (ret) {
		printf("pthread setschedpolicy failed\n");
		goto out;
	}
	param.sched_priority = 80;
	ret = pthread_attr_setschedparam(&attr, &param);
	if (ret) {
		printf("pthread setschedparam failed\n");
		goto out;
	}
	/* Use scheduling parameters of attr */
	ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	if (ret) {
		printf("pthread setinheritsched failed\n");
		goto out;
	}

	/* Create a pthread with specified attributes */
	ret = pthread_create(thread, &attr, thread_func, arg);
	if (ret) {
		printf("create pthread failed\n");
		goto out;
	}

out:
	return ret;
}

static void calc_time_diff(const struct timespec *lo,
			   const struct timespec *hi,
			   struct timespec *diff)
{
	diff->tv_sec = hi->tv_sec - lo->tv_sec;
	diff->tv_nsec = hi->tv_nsec - lo->tv_nsec;
	if (diff->tv_nsec < 0) {
		diff->tv_sec--;
		diff->tv_nsec += 1000000000;
	}
}

static void *can_tx_thread_fn(void *data)
{
	ssize_t wrlen, prlen;
	struct timespec start_time, end_time, elapsed_time, sleep_time;
	char print_buf[4096];

	(void)data;

	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &start_time);
		wrlen = can_write(can_hdl, can_tx_frame);
		if (wrlen < 0) {
			printf("can_write errno: %d\n", errno);
			break;
		}
		if (verbose) {
			prlen = snprintf(print_buf, sizeof(print_buf),
					 "[%6lu.%06ld] TX: ",
					 (unsigned long)start_time.tv_sec,
					 start_time.tv_nsec / 1000);
			sprint_canframe(print_buf + prlen, can_rx_frame, 0,
					CANFD_MAX_DLEN);
			printf("%s\n", print_buf);
		}
		if (!tx_interval.tv_sec && !tx_interval.tv_nsec)
			/* Terminate TX thread if TX interval is zero */
			break;

		clock_gettime(CLOCK_MONOTONIC, &end_time);

		calc_time_diff(&start_time, &end_time, &elapsed_time);
		calc_time_diff(&elapsed_time, &tx_interval, &sleep_time);

		if (sleep_time.tv_sec < 0) {
			/* Skip sleep if sleep_time is negative */
			printf("Elapsed time ([%6lu.%06ld]) greater than TX interval. Skipping sleep!\n",
			       (unsigned long)elapsed_time.tv_sec,
			       elapsed_time.tv_nsec / 1000);
			continue;
		}

		clock_nanosleep(CLOCK_MONOTONIC, 0, &sleep_time, NULL);
	}

	return NULL;
}

static void *can_rx_thread_fn(void *data)
{
	ssize_t rdlen, prlen;
	struct timespec ts;
	char print_buf[4096];

	(void)data;

	for (;;) {
		rdlen = can_read(can_hdl, can_rx_frame);
		if (rdlen < 0) {
			printf("can_read errno %d\n", errno);
			break;
		}

		clock_gettime(CLOCK_MONOTONIC, &ts);
		prlen = snprintf(print_buf, sizeof(print_buf),
				 "[%6lu.%06ld] RX: ",
				 (unsigned long)ts.tv_sec,
				 ts.tv_nsec / 1000);
		sprint_canframe(print_buf + prlen, can_rx_frame, 0,
				CANFD_MAX_DLEN);
		printf("%s\n", print_buf);
	}

	return NULL;
}

static void print_usage(char *argv0)
{
	printf("Usage:\n");
	printf("%s OPTIONS\n", argv0);
	printf("\n");
	printf("rt-can-test can be used to continuously transmit and/or\n");
	printf("receive CAN frames using the socket CAN API.\n");
	printf("\n");
	printf("The main purpose of the tool is to test the realtime\n");
	printf("behaviour of CAN subsystem in a Linux system.\n");
	printf("\n");
	printf("Options:\n");
	printf("  --if, --interface  CAN interface. Mandatory option!\n");
	printf("  -t, --tx           Transmit CAN frames with an interval\n");
	printf("                     specified by the --tx-interval option.\n");
	printf("                     If no --tx-interval option is provided\n");
	printf("                     only one CAN frame will be transmitted.\n");
	printf("  -i, --tx-interval  TX interval in microseconds of CAN frames.\n");
	printf("                     This option has no effect if the --tx option\n");
	printf("                     is omitted\n");
	printf("  -r, --rx           Receive CAN frames and print the hex output to\n");
	printf("                     stdout if the --verbose option is set.\n");
	printf("  -v, --verbose      Enable debug prints.\n");
	printf("  -h, --help         Print this help and exit.\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	pthread_t tx_thread, rx_thread;
	int ret, opt, optind = 0, required_mtu;
	long tx_int_usec;
	struct option long_opts[] = {
		{"help", no_argument, 0, 'h'},
		{"if", required_argument, 0, 1000},
		{"interface", required_argument, 0, 1001},
		{"tx", required_argument, 0, 't'},
		{"rx", no_argument, 0, 'r'},
		{"tx-interval", required_argument, 0, 'i'},
		{"verbose", no_argument, 0, 'v'},
		{NULL, 0, 0, 0},
	};
	struct can_cfg cfg = {0};

	setup_mem();

	while ((opt = getopt_long(argc, argv, "t:ri:vh", long_opts, &optind)) != -1) {
		switch (opt) {
		case 1000:
		/* Fallthrough */
		case 1001:
			cfg.ifname = optarg;
			break;
		case 't':
			can_tx_frame = malloc(sizeof(*can_tx_frame));
			memset(can_tx_frame, 0, sizeof(*can_tx_frame));
			required_mtu = parse_canframe(optarg, can_tx_frame);
			/* ensure discrete CAN FD length values:
			 * 0..8, 12, 16, 20, 24, 32, 64
			 */
			cfg.mtu = required_mtu;
			can_tx_frame->len = can_dlc2len(can_len2dlc(can_tx_frame->len));
			run_tx_test = true;
			break;
		case 'r':
			run_rx_test = true;
			break;
		case 'i':
			tx_int_usec = strtol(optarg, NULL, 0);
			tx_interval.tv_sec = tx_int_usec / 1000000;
			tx_interval.tv_nsec = (tx_int_usec * 1000) %
					      1000000000;
			break;
		case 'v':
			verbose = true;
			break;
		case 'h':
		default:
			print_usage(argv[0]);
			return 0;
		}
	}

	if (!cfg.ifname) {
		printf("Missing CAN interface!\n");
		ret = -1;
		goto out;
	}

	if (!run_tx_test && !run_rx_test) {
		printf("At least one of the --rx and --tx options must be used!\n");
		ret = -1;
		goto out;
	}

	ret = can_open(&can_hdl, &cfg);
	if (ret) {
		printf("Unable to open CAN interface: %s\n",
			cfg.ifname);
		goto out;
	}

	if (run_tx_test) {
		ret = create_thread(&tx_thread, can_tx_thread_fn, NULL);
		if (ret)
			goto out;
	}

	if (run_rx_test) {
		can_rx_frame = malloc(sizeof(*can_rx_frame));
		memset(can_rx_frame, 0, sizeof(*can_rx_frame));

		ret = create_thread(&rx_thread, can_rx_thread_fn, NULL);
		if (ret)
			goto out;
	}

	if (run_tx_test) {
		ret = pthread_join(tx_thread, NULL);
		if (ret)
			printf("join TX thread failed: %d\n", ret);
	}

	if (run_rx_test) {
		ret = pthread_join(rx_thread, NULL);
		if (ret)
			printf("join RX thread failed: %d\n", ret);
	}

out:
	return ret;
}
