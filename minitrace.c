// See minitrace.h for basic documentation.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "minitrace.h"

typedef struct raw_event {
	const char *name;
	const char *cat;
	uint64_t ts;
	uint32_t pid;
	uint32_t tid;
	char ph;
	// We don't bother with pid or args at the moment.
} raw_event_t;

static raw_event_t *buffer;
static volatile int count;
static int started = 0;
static uint64_t time_offset;
static int first_line = 1;
static FILE *f;
static __thread int cur_thread_id;

// Tiny portability layer.
// Exposes:
//	 get_cur_thread_id()
//	 real_time_s()
#ifdef _WIN32
static inline int get_cur_thread_id() {
	return (int)GetCurrentThreadId();
}

static uint64_t _frequency = 0;
static uint64_t _starttime = 0;
static double real_time_s() {
	if (_frequency == 0) {
		QueryPerformanceFrequency((LARGE_INTEGER*)&_frequency);
		QueryPerformanceCounter((LARGE_INTEGER*)&_starttime);
		curtime = 0;
	}
	__int64 time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return ((double) (time - _starttime) / (double) _frequency);
}
#else

static inline int get_cur_thread_id() {
	return (int)pthread_self();
}

#if defined(BLACKBERRY)
static double real_time_s() {
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time); // Linux must use CLOCK_MONOTONIC_RAW due to time warps
	return time.tv_sec + time.tv_nsec / 1.0e9;
}
#else
static double real_time_s() {
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

#endif

void mtr_init(const char *json_file) {
	buffer = malloc(BUFFER_SIZE * sizeof(raw_event_t));
	started = 1;
	count = 0;
	f = fopen(json_file, "wb");
	const char *header = "{\"traceEvents\":[\n";
	fwrite(header, 1, strlen(header), f);
	time_offset = real_time_s();
	first_line = 1;
}

void mtr_shutdown() {
	fwrite("\n]}\n", 1, 4, f);
	mtr_flush();
	fclose(f);
	f = 0;
	free(buffer);
	buffer = 0;
}

void mtr_start() {
	started = 1;
}

void mtr_stop() {
	started = 0;
}

// TODO: fwrite more than one line at a time.
void mtr_flush() {
	int i = 0, cnt = 0;
	char linebuf[256];

retry:
	cnt = count;
	for (i = 0; i < cnt; i++) {
		raw_event_t *raw = &buffer[i];
		int len = sprintf(linebuf, "%s{\"cat\":\"%s\",\"pid\":%i,\"tid\":%i,\"ts\":%llu,\"ph\":\"%c\",\"name\":\"%s\",\"args\":{}}",
			first_line ? "" : ",\n",
			raw->cat, raw->pid, raw->tid, raw->ts - time_offset, raw->ph, raw->name);
		fwrite(linebuf, 1, len, f);
		first_line = 0;
	}

	// Yeah, so this isn't really threadsafe at all.
	if (cnt < count)
		goto retry;
	count = 0;
}

// Gotta be fast!
void mtr_raw_event(const char *category, const char *name, char ph) {
	if (count >= BUFFER_SIZE || !started)
		return;
	double ts = real_time_s(); 
	raw_event_t *ev = &buffer[count++];
	ev->cat = category;
	ev->name = name;
	ev->ts = (int64_t)(ts * 1000000);
	ev->ph = ph;
	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	ev->tid = cur_thread_id;
	ev->pid = 0;
}
