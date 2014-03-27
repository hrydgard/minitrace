// minitrace
// by Henrik Rydgård 2014
// http://www.github.com/hrydgard/minitrace
// Released under the MIT license.

// See minitrace.h for basic documentation.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#pragma warning (disable:4996)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define __thread __declspec(thread)
#else
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "minitrace.h"

// Ugh, this struct is already pretty heavy.
// Will probably need to move arguments to a second buffer to support more than one.
typedef struct raw_event {
	const char *name;
	const char *cat;
	void *id;
	uint64_t ts;
	uint32_t pid;
	uint32_t tid;
	char ph;
	mtr_arg_type arg_type;
	const char *arg_name;
	union {
		const char *a_str;
		int a_int;
		double a_double;
	};
} raw_event_t;

static raw_event_t *buffer;
static volatile int count;
static int tracing = 0;
static uint64_t time_offset;
static int first_line = 1;
static FILE *f;
static __thread int cur_thread_id;

#define STRING_POOL_SIZE 100
static char *str_pool[100];

// Tiny portability layer.
// Exposes:
//	 get_cur_thread_id()
//	 mtr_time_s()
#ifdef _WIN32
static int get_cur_thread_id() {
	return (int)GetCurrentThreadId();
}

static uint64_t _frequency = 0;
static uint64_t _starttime = 0;
double mtr_time_s() {
	if (_frequency == 0) {
		QueryPerformanceFrequency((LARGE_INTEGER*)&_frequency);
		QueryPerformanceCounter((LARGE_INTEGER*)&_starttime);
	}
	__int64 time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return ((double) (time - _starttime) / (double) _frequency);
}

void mtr_register_sigint_handler() {
	// TODO
}

#else

static inline int get_cur_thread_id() {
	return (int)(intptr_t)pthread_self();
}

#if defined(BLACKBERRY)
double mtr_time_s() {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time); // Linux must use CLOCK_MONOTONIC_RAW due to time warps
	return time.tv_sec + time.tv_nsec / 1.0e9;
}
#else
double mtr_time_s() {
	static time_t start;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	if (start == 0) {
		start = tv.tv_sec;
	}
	tv.tv_sec -= start;
	return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}
#endif	// !BLACKBERRY

static void termination_handler(int signum) {
	if (tracing) {
		printf("Ctrl-C detected! Flushing trace info and shutting down.\n");
		mtr_flush();
		fwrite("\n]}\n", 1, 4, f);
		fclose(f);
		exit(1);
	}
}

void mtr_register_sigint_handler() {
	// Avoid altering set-to-be-ignored handlers while registering.
	if (signal(SIGINT, &termination_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
}

#endif

void mtr_init(const char *json_file) {
	buffer = (raw_event_t *)malloc(BUFFER_SIZE * sizeof(raw_event_t));
	tracing = 1;
	count = 0;
	f = fopen(json_file, "wb");
	const char *header = "{\"traceEvents\":[\n";
	fwrite(header, 1, strlen(header), f);
	time_offset = (uint64_t)(mtr_time_s() * 1000000);
	first_line = 1;
}

void mtr_shutdown() {
	mtr_flush();
	fwrite("\n]}\n", 1, 4, f);
	fclose(f);
	f = 0;
	free(buffer);
	buffer = 0;
	for (int i = 0; i < STRING_POOL_SIZE; i++) {
		if (str_pool[i]) {
			free(str_pool[i]);
			str_pool[i] = 0;
		}
	}
}

const char *mtr_pool_string(const char *str) {
	for (int i = 0; i < STRING_POOL_SIZE; i++) {
		if (!str_pool[i]) {
			str_pool[i] = malloc(strlen(str) + 1);
			strcpy(str_pool[i], str);
			return str_pool[i];
		} else {
			if (!strcmp(str, str_pool[i]))
				return str_pool[i];
		}
	}
	return "string pool full";
}

void mtr_start() {
	tracing = 1;
}

void mtr_stop() {
	tracing = 0;
}

// TODO: fwrite more than one line at a time.
void mtr_flush() {
	int i = 0, cnt = 0;
	char linebuf[1024];
	char arg_buf[256];
	char id_buf[256];
retry:
	cnt = count;
	for (i = 0; i < cnt; i++) {
		raw_event_t *raw = &buffer[i];
		int len;
		switch (raw->arg_type) {
		case MTR_ARG_TYPE_INT:
			sprintf(arg_buf, "\"%s\":%i", raw->arg_name, raw->a_int);
			break;
		case MTR_ARG_TYPE_STRING_CONST:
			sprintf(arg_buf, "\"%s\":\"%s\"", raw->arg_name, raw->a_str);
			break;
		case MTR_ARG_TYPE_STRING_COPY:
		if (strlen(raw->a_str) > 700) {
			((char*)raw->a_str)[700] = 0;
		}
			sprintf(arg_buf, "\"%s\":\"%s\"", raw->arg_name, raw->a_str);
			break;
		case MTR_ARG_TYPE_NONE:
		default:
			arg_buf[0] = '\0';
			break;
		}
		if (raw->id) {
			switch (raw->ph) {
			case 'S':
			case 'T':
			case 'F':
				// TODO: Support full 64-bit pointers
				sprintf(id_buf, ",\"id\":\"0x%08x\"", (uint32_t)(uintptr_t)raw->id);
				break;
			case 'X':
				sprintf(id_buf, ",\"dur\":%i", (int)raw->a_double);
				break;
			}
		} else {
			id_buf[0] = 0;
		}
		len = sprintf(linebuf, "%s{\"cat\":\"%s\",\"pid\":%i,\"tid\":%i,\"ts\":%llu,\"ph\":\"%c\",\"name\":\"%s\",\"args\":{%s}%s}",
				first_line ? "" : ",\n",
				raw->cat, raw->pid, raw->tid, raw->ts - time_offset, raw->ph, raw->name, arg_buf, id_buf);
		fwrite(linebuf, 1, len, f);
		first_line = 0;
	}

	// Yeah, so this isn't really threadsafe at all.
	if (cnt < count)
		goto retry;
	count = 0;
}

// Gotta be fast!
void internal_mtr_raw_event(const char *category, const char *name, char ph, void *id) {
	if (count >= BUFFER_SIZE || !tracing)
		return;
	double ts = mtr_time_s();
	raw_event_t *ev = &buffer[count++];
	ev->cat = category;
	ev->name = name;
	ev->id = id;
	ev->ph = ph;
	if (ev->ph == 'X') {
		double x;
		memcpy(&x, id, sizeof(double));
		ev->ts = x * 1000000;
		ev->a_double = (ts - x) * 1000000;
	} else {
		ev->ts = (int64_t)(ts * 1000000);
	}
	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	ev->tid = cur_thread_id;
	ev->pid = 0;
}

// Gotta be fast!
void internal_mtr_raw_event_arg(const char *category, const char *name, char ph, void *id, mtr_arg_type arg_type, const char *arg_name, void *arg_value) {
	if (count >= BUFFER_SIZE || !tracing)
		return;
	double ts = mtr_time_s();
	raw_event_t *ev = &buffer[count++];
	ev->cat = category;
	ev->name = name;
	ev->id = id;
	ev->ts = (int64_t)(ts * 1000000);
	ev->ph = ph;
	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	ev->tid = cur_thread_id;
	ev->pid = 0;
	ev->arg_type = arg_type;
	ev->arg_name = arg_name;
	switch (arg_type) {
	case MTR_ARG_TYPE_INT: ev->a_int = (int)(uintptr_t)arg_value; break;
	case MTR_ARG_TYPE_STRING_CONST:	ev->a_str = (const char*)arg_value; break;
	case MTR_ARG_TYPE_STRING_COPY: ev->a_str = strdup((const char*)arg_value); break;
	default:
		break;
	}
}

