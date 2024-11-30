// Minitrace
//
// Copyright 2014 by Henrik Rydgård
// http://www.github.com/hrydgard/minitrace
// Released under the MIT license.
//
// Ultra-light dependency free library for performance tracing C/C++ applications.
// Produces traces compatible with Google Chrome's trace viewer.
// Simply open "about:tracing" in Chrome and load the produced JSON.
//
// This contains far less template magic than the original libraries from Chrome
// because this is meant to be usable from C.
//
// See README.md for a tutorial.
//
// The trace format is documented here:
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit
// More:
// http://www.altdevblogaday.com/2012/08/21/using-chrometracing-to-view-your-inline-profiling-data/

#ifndef MINITRACE_H
#define MINITRACE_H

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#pragma warning (disable:4996)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define __thread __declspec(thread)
#define pthread_mutex_t CRITICAL_SECTION
#define pthread_mutex_init(a, b) InitializeCriticalSection(a)
#define pthread_mutex_lock(a) EnterCriticalSection(a)
#define pthread_mutex_unlock(a) LeaveCriticalSection(a)
#define pthread_mutex_destroy(a) DeleteCriticalSection(a)
#else
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef __GNUC__
#define ATTR_NORETURN __attribute__((noreturn))
#else
#define ATTR_NORETURN
#endif

#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])
#define TRUE 1
#define FALSE 0


// If MTR_ENABLED is not defined, Minitrace does nothing and has near zero overhead.
// Preferably, set this flag in your build system. If you can't just uncomment this line.
#define MTR_ENABLED

// By default, will collect up to 1000000 events, then you must flush.
// It's recommended that you simply call mtr_flush on a background thread
// occasionally. It's safe...ish.
#define INTERNAL_MINITRACE_BUFFER_SIZE 1000000

#ifdef __cplusplus
extern "C" {
#endif

// Commented-out types will be supported in the future.
typedef enum {
	MTR_ARG_TYPE_NONE = 0,
	MTR_ARG_TYPE_INT = 1,	// I
	// MTR_ARG_TYPE_FLOAT = 2,  // TODO
	// MTR_ARG_TYPE_DOUBLE = 3,  // TODO
	MTR_ARG_TYPE_STRING_CONST = 8,	// C
	MTR_ARG_TYPE_STRING_COPY = 9,
	// MTR_ARG_TYPE_JSON_COPY = 10,
} mtr_arg_type;

// TODO: Add support for more than one argument (metadata) per event
// Having more costs speed and memory.
#define MTR_MAX_ARGS 1

// Ugh, this struct is already pretty heavy.
// Will probably need to move arguments to a second buffer to support more than one.
typedef struct raw_event {
	const char *name;
	const char *cat;
	void *id;
	int64_t ts;
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

static raw_event_t *event_buffer;
static raw_event_t *flush_buffer;
static volatile int event_count;
static int is_tracing = FALSE;
static int is_flushing = FALSE;
static int events_in_progress = 0;
static int64_t time_offset;
static int first_line = 1;
static FILE *f;
static __thread int cur_thread_id;	// Thread local storage
static int cur_process_id;
static pthread_mutex_t mutex;
static pthread_mutex_t event_mutex;

#define STRING_POOL_SIZE 100
static char *str_pool[STRING_POOL_SIZE];

void mtr_flush_with_state(int is_last) {
#ifndef MTR_ENABLED
	return;
#endif
	int i = 0;
	char linebuf[1024];
	char arg_buf[1024];
	char id_buf[256];
	int event_count_copy = 0;
	int events_in_progress_copy = 1;
	raw_event_t *event_buffer_tmp = NULL;

	// small critical section to swap buffers
	// - no any new events can be spawn while
	//   swapping since they tied to the same mutex
	// - checks for any flushing in process
	pthread_mutex_lock(&mutex);
	// if not flushing already
	if (is_flushing) {
		pthread_mutex_unlock(&mutex);
		return;
	}
	is_flushing = TRUE;
	event_count_copy = event_count;
	event_buffer_tmp = flush_buffer;
	flush_buffer = event_buffer;
	event_buffer = event_buffer_tmp;
	event_count = 0;
	// waiting for any unfinished events before swap
	while (events_in_progress_copy != 0) {
		pthread_mutex_lock(&event_mutex);
		events_in_progress_copy = events_in_progress;
		pthread_mutex_unlock(&event_mutex);
	}
	pthread_mutex_unlock(&mutex);

	for (i = 0; i < event_count_copy; i++) {
		raw_event_t *raw = &flush_buffer[i];
		int len;
		switch (raw->arg_type) {
		case MTR_ARG_TYPE_INT:
			snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":%i", raw->arg_name, raw->a_int);
			break;
		case MTR_ARG_TYPE_STRING_CONST:
			snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":\"%s\"", raw->arg_name, raw->a_str);
			break;
		case MTR_ARG_TYPE_STRING_COPY:
			if (strlen(raw->a_str) > 700) {
				snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":\"%.*s\"", raw->arg_name, 700, raw->a_str);
			} else {
				snprintf(arg_buf, ARRAY_SIZE(arg_buf), "\"%s\":\"%s\"", raw->arg_name, raw->a_str);
			}
			break;
		case MTR_ARG_TYPE_NONE:
			arg_buf[0] = '\0';
			break;
		}
		if (raw->id) {
			switch (raw->ph) {
			case 'S':
			case 'T':
			case 'F':
				// TODO: Support full 64-bit pointers
				snprintf(id_buf, ARRAY_SIZE(id_buf), ",\"id\":\"0x%08x\"", (uint32_t)(uintptr_t)raw->id);
				break;
			case 'X':
				snprintf(id_buf, ARRAY_SIZE(id_buf), ",\"dur\":%i", (int)raw->a_double);
				break;
			}
		} else {
			id_buf[0] = 0;
		}
		const char *cat = raw->cat;
#ifdef _WIN32
		// On Windows, we often end up with backslashes in category.
		char temp[256];
		{
			int len = (int)strlen(cat);
			int i;
			if (len > 255) len = 255;
			for (i = 0; i < len; i++) {
				temp[i] = cat[i] == '\\' ? '/' : cat[i];
			}
			temp[len] = 0;
			cat = temp;
		}
#endif

		len = snprintf(linebuf, ARRAY_SIZE(linebuf), "%s{\"cat\":\"%s\",\"pid\":%i,\"tid\":%i,\"ts\":%" PRId64 ",\"ph\":\"%c\",\"name\":\"%s\",\"args\":{%s}%s}",
				first_line ? "" : ",\n",
				cat, raw->pid, raw->tid, raw->ts - time_offset, raw->ph, raw->name, arg_buf, id_buf);
		fwrite(linebuf, 1, len, f);
		first_line = 0;

		if (raw->arg_type == MTR_ARG_TYPE_STRING_COPY) {
			free((void*)raw->a_str);
		}
		#ifdef MTR_COPY_EVENT_CATEGORY_AND_NAME
		free(raw->name);
		free(raw->cat);
		#endif
	}

	pthread_mutex_lock(&mutex);
	is_flushing = is_last;
	pthread_mutex_unlock(&mutex);
}

// Flushes the collected data to disk, clearing the buffer for new data.
void mtr_flush() {
	mtr_flush_with_state(FALSE);
}

void mtr_shutdown() {
	int i;
#ifndef MTR_ENABLED
	return;
#endif
	pthread_mutex_lock(&mutex);
	is_tracing = FALSE;
	pthread_mutex_unlock(&mutex);
	mtr_flush_with_state(TRUE);

	fwrite("\n]}\n", 1, 4, f);
	fclose(f);
	pthread_mutex_destroy(&mutex);
	pthread_mutex_destroy(&event_mutex);
	f = 0;
	free(event_buffer);
	event_buffer = 0;
	for (i = 0; i < STRING_POOL_SIZE; i++) {
		if (str_pool[i]) {
			free(str_pool[i]);
			str_pool[i] = 0;
		}
	}
}

#ifdef _WIN32
static int get_cur_thread_id() {
	return (int)GetCurrentThreadId();
}
static int get_cur_process_id() {
	return (int)GetCurrentProcessId();
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

// Ctrl+C handling for Windows console apps
static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {
	if (is_tracing && fdwCtrlType == CTRL_C_EVENT) {
		printf("Ctrl-C detected! Flushing trace and shutting down.\n\n");
		mtr_flush();
		mtr_shutdown();
	}
	ExitProcess(1);
}

void mtr_register_sigint_handler() {
	// For console apps:
	SetConsoleCtrlHandler(&CtrlHandler, TRUE);
}

#else

static inline int get_cur_thread_id() {
	return (int)(intptr_t)pthread_self();
}
static inline int get_cur_process_id() {
	return (int)getpid();
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

static void termination_handler(int signum) ATTR_NORETURN;
static void termination_handler(int signum) {
	(void) signum;
	if (is_tracing) {
		printf("Ctrl-C detected! Flushing trace and shutting down.\n\n");
		mtr_flush();
		fwrite("\n]}\n", 1, 4, f);
		fclose(f);
	}
	exit(1);
}

void mtr_register_sigint_handler() {
#ifndef MTR_ENABLED
	return;
#endif // MTR_ENABLED
	// Avoid altering set-to-be-ignored handlers while registering.
	if (signal(SIGINT, &termination_handler) == SIG_IGN)
		signal(SIGINT, SIG_IGN);
}

#endif // WIN

// Same as below, but allows passing in a custom stream (FILE *), as returned by
// fopen(). It should be opened for writing, preferably in binary mode to avoid
// processing of line endings (i.e. the "wb" mode).
void mtr_init_from_stream(void *stream) {
#ifndef MTR_ENABLED
	return;
#endif
	event_buffer = (raw_event_t *)malloc(INTERNAL_MINITRACE_BUFFER_SIZE * sizeof(raw_event_t));
	flush_buffer = (raw_event_t *)malloc(INTERNAL_MINITRACE_BUFFER_SIZE * sizeof(raw_event_t));
	is_tracing = 1;
	event_count = 0;
	f = (FILE *)stream;
	const char *header = "{\"traceEvents\":[\n";
	fwrite(header, 1, strlen(header), f);
	time_offset = (uint64_t)(mtr_time_s() * 1000000);
	first_line = 1;
	pthread_mutex_init(&mutex, 0);
	pthread_mutex_init(&event_mutex, 0);
}

// Initializes Minitrace. Must be called very early during startup of your executable,
// before any MTR_ statements.
void mtr_init(const char *json_file) {
#ifndef MTR_ENABLED
	return;
#endif
	mtr_init_from_stream(fopen(json_file, "wb"));
}

// Lets you enable and disable Minitrace at runtime.
// May cause strange discontinuities in the output.
// Minitrace is enabled on startup by default.
void mtr_start() {
#ifndef MTR_ENABLED
	return;
#endif
	pthread_mutex_lock(&mutex);
	is_tracing = TRUE;
	pthread_mutex_unlock(&mutex);
}
void mtr_stop() {
#ifndef MTR_ENABLED
	return;
#endif
	pthread_mutex_lock(&mutex);
	is_tracing = FALSE;
	pthread_mutex_unlock(&mutex);
}


// Utility function that should rarely be used.
// If str is semi dynamic, store it permanently in a small pool so we don't need to malloc it.
// The pool fills up fast though and performance isn't great.
// Returns a fixed string if the pool is full.
const char *mtr_pool_string(const char *str) {
	int i;
	for (i = 0; i < STRING_POOL_SIZE; i++) {
		if (!str_pool[i]) {
			str_pool[i] = (char*)malloc(strlen(str) + 1);
			strcpy(str_pool[i], str);
			return str_pool[i];
		} else {
			if (!strcmp(str, str_pool[i]))
				return str_pool[i];
		}
	}
	return "string pool full";
}

// Only use the macros to call these.
void internal_mtr_raw_event(const char *category, const char *name, char ph, void *id) {
#ifndef MTR_ENABLED
	return;
#endif
	pthread_mutex_lock(&mutex);
	if (!is_tracing || event_count >= INTERNAL_MINITRACE_BUFFER_SIZE) {
		pthread_mutex_unlock(&mutex);
		return;
	}
	raw_event_t *ev = &event_buffer[event_count];
	++event_count;
	pthread_mutex_lock(&event_mutex);
	++events_in_progress;
	pthread_mutex_unlock(&event_mutex);
	pthread_mutex_unlock(&mutex);

	double ts = mtr_time_s();
	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	if (!cur_process_id) {
		cur_process_id = get_cur_process_id();
	}

#ifdef MTR_COPY_EVENT_CATEGORY_AND_NAME
	const size_t category_len = strlen(category);
	ev->cat = malloc(category_len + 1);
	strcpy(ev->cat, category);

	const size_t name_len = strlen(name);
	ev->name = malloc(name_len + 1);
	strcpy(ev->name, name);

#else
	ev->cat = category;
	ev->name = name;
#endif

	ev->id = id;
	ev->ph = ph;
	if (ev->ph == 'X') {
		double x;
		memcpy(&x, id, sizeof(double));
		ev->ts = (int64_t)(x * 1000000);
		ev->a_double = (ts - x) * 1000000;
	} else {
		ev->ts = (int64_t)(ts * 1000000);
	}
	ev->tid = cur_thread_id;
	ev->pid = cur_process_id;
	ev->arg_type = MTR_ARG_TYPE_NONE;

	pthread_mutex_lock(&event_mutex);
	--events_in_progress;
	pthread_mutex_unlock(&event_mutex);
}
void internal_mtr_raw_event_arg(const char *category, const char *name, char ph, void *id, mtr_arg_type arg_type, const char *arg_name, void *arg_value) {
#ifndef MTR_ENABLED
	return;
#endif
	pthread_mutex_lock(&mutex);
	if (!is_tracing || event_count >= INTERNAL_MINITRACE_BUFFER_SIZE) {
		pthread_mutex_unlock(&mutex);
		return;
	}
	raw_event_t *ev = &event_buffer[event_count];
	++event_count;
	pthread_mutex_lock(&event_mutex);
	++events_in_progress;
	pthread_mutex_unlock(&event_mutex);
	pthread_mutex_unlock(&mutex);

	if (!cur_thread_id) {
		cur_thread_id = get_cur_thread_id();
	}
	if (!cur_process_id) {
		cur_process_id = get_cur_process_id();
	}
	double ts = mtr_time_s();

#ifdef MTR_COPY_EVENT_CATEGORY_AND_NAME
	const size_t category_len = strlen(category);
	ev->cat = malloc(category_len + 1);
	strcpy(ev->cat, category);

	const size_t name_len = strlen(name);
	ev->name = malloc(name_len + 1);
	strcpy(ev->name, name);

#else
	ev->cat = category;
	ev->name = name;
#endif

	ev->id = id;
	ev->ts = (int64_t)(ts * 1000000);
	ev->ph = ph;
	ev->tid = cur_thread_id;
	ev->pid = cur_process_id;
	ev->arg_type = arg_type;
	ev->arg_name = arg_name;
	switch (arg_type) {
	case MTR_ARG_TYPE_INT: ev->a_int = (int)(uintptr_t)arg_value; break;
	case MTR_ARG_TYPE_STRING_CONST:	ev->a_str = (const char*)arg_value; break;
	case MTR_ARG_TYPE_STRING_COPY: ev->a_str = strdup((const char*)arg_value); break;
	case MTR_ARG_TYPE_NONE: break;
	}

	pthread_mutex_lock(&event_mutex);
	--events_in_progress;
	pthread_mutex_unlock(&event_mutex);
}

#ifdef MTR_ENABLED

// c - category. Can be filtered by in trace viewer (or at least that's the intention).
//     A good use is to pass __FILE__, there are macros further below that will do it for you.
// n - name. Pass __FUNCTION__ in most cases, unless you are marking up parts of one.

// Scopes. In C++, use MTR_SCOPE. In C, always match them within the same scope.
#define MTR_BEGIN(c, n) internal_mtr_raw_event(c, n, 'B', 0)
#define MTR_END(c, n) internal_mtr_raw_event(c, n, 'E', 0)
#define MTR_SCOPE(c, n) MTRScopedTrace ____mtr_scope(c, n)
#define MTR_SCOPE_LIMIT(c, n, l) MTRScopedTraceLimit ____mtr_scope(c, n, l)

// Async events. Can span threads. ID identifies which events to connect in the view.
#define MTR_START(c, n, id) internal_mtr_raw_event(c, n, 'S', (void *)(id))
#define MTR_STEP(c, n, id, step) internal_mtr_raw_event_arg(c, n, 'T', (void *)(id), MTR_ARG_TYPE_STRING_CONST, "step", (void *)(step))
#define MTR_FINISH(c, n, id) internal_mtr_raw_event(c, n, 'F', (void *)(id))

// Flow events. Like async events, but displayed in a more fancy way in the viewer.
#define MTR_FLOW_START(c, n, id) internal_mtr_raw_event(c, n, 's', (void *)(id))
#define MTR_FLOW_STEP(c, n, id, step) internal_mtr_raw_event_arg(c, n, 't', (void *)(id), MTR_ARG_TYPE_STRING_CONST, "step", (void *)(step))
#define MTR_FLOW_FINISH(c, n, id) internal_mtr_raw_event(c, n, 'f', (void *)(id))

// The same macros, but with a single named argument which shows up as metadata in the viewer.
// _I for int.
// _C is for a const string arg.
// _S will copy the string, freeing on flush (expensive but sometimes necessary).
// but required if the string was generated dynamically.

// Note that it's fine to match BEGIN_S with END and BEGIN with END_S, etc.
#define MTR_BEGIN_C(c, n, aname, astrval) internal_mtr_raw_event_arg(c, n, 'B', 0, MTR_ARG_TYPE_STRING_CONST, aname, (void *)(astrval))
#define MTR_END_C(c, n, aname, astrval) internal_mtr_raw_event_arg(c, n, 'E', 0, MTR_ARG_TYPE_STRING_CONST, aname, (void *)(astrval))
#define MTR_SCOPE_C(c, n, aname, astrval) MTRScopedTraceArg ____mtr_scope(c, n, MTR_ARG_TYPE_STRING_CONST, aname, (void *)(astrval))

#define MTR_BEGIN_S(c, n, aname, astrval) internal_mtr_raw_event_arg(c, n, 'B', 0, MTR_ARG_TYPE_STRING_COPY, aname, (void *)(astrval))
#define MTR_END_S(c, n, aname, astrval) internal_mtr_raw_event_arg(c, n, 'E', 0, MTR_ARG_TYPE_STRING_COPY, aname, (void *)(astrval))
#define MTR_SCOPE_S(c, n, aname, astrval) MTRScopedTraceArg ____mtr_scope(c, n, MTR_ARG_TYPE_STRING_COPY, aname, (void *)(astrval))

#define MTR_BEGIN_I(c, n, aname, aintval) internal_mtr_raw_event_arg(c, n, 'B', 0, MTR_ARG_TYPE_INT, aname, (void*)(intptr_t)(aintval))
#define MTR_END_I(c, n, aname, aintval) internal_mtr_raw_event_arg(c, n, 'E', 0, MTR_ARG_TYPE_INT, aname, (void*)(intptr_t)(aintval))
#define MTR_SCOPE_I(c, n, aname, aintval) MTRScopedTraceArg ____mtr_scope(c, n, MTR_ARG_TYPE_INT, aname, (void*)(intptr_t)(aintval))

// Instant events. For things with no duration.
#define MTR_INSTANT(c, n) internal_mtr_raw_event(c, n, 'I', 0)
#define MTR_INSTANT_C(c, n, aname, astrval) internal_mtr_raw_event_arg(c, n, 'I', 0, MTR_ARG_TYPE_STRING_CONST, aname, (void *)(astrval))
#define MTR_INSTANT_I(c, n, aname, aintval) internal_mtr_raw_event_arg(c, n, 'I', 0, MTR_ARG_TYPE_INT, aname, (void *)(aintval))

// Counters (can't do multi-value counters yet)
#define MTR_COUNTER(c, n, val) internal_mtr_raw_event_arg(c, n, 'C', 0, MTR_ARG_TYPE_INT, n, (void *)(intptr_t)(val))

// Metadata. Call at the start preferably. Must be const strings.

#define MTR_META_PROCESS_NAME(n) internal_mtr_raw_event_arg("", "process_name", 'M', 0, MTR_ARG_TYPE_STRING_COPY, "name", (void *)(n))
#define MTR_META_THREAD_NAME(n) internal_mtr_raw_event_arg("", "thread_name", 'M', 0, MTR_ARG_TYPE_STRING_COPY, "name", (void *)(n))
#define MTR_META_THREAD_SORT_INDEX(i) internal_mtr_raw_event_arg("", "thread_sort_index", 'M', 0, MTR_ARG_TYPE_INT, "sort_index", (void *)(i))

#else

#define MTR_BEGIN(c, n)
#define MTR_END(c, n)
#define MTR_SCOPE(c, n)
#define MTR_START(c, n, id)
#define MTR_STEP(c, n, id, step)
#define MTR_FINISH(c, n, id)
#define MTR_FLOW_START(c, n, id)
#define MTR_FLOW_STEP(c, n, id, step)
#define MTR_FLOW_FINISH(c, n, id)
#define MTR_INSTANT(c, n)

#define MTR_BEGIN_C(c, n, aname, astrval)
#define MTR_END_C(c, n, aname, astrval)
#define MTR_SCOPE_C(c, n, aname, astrval)

#define MTR_BEGIN_S(c, n, aname, astrval)
#define MTR_END_S(c, n, aname, astrval)
#define MTR_SCOPE_S(c, n, aname, astrval)

#define MTR_BEGIN_I(c, n, aname, aintval)
#define MTR_END_I(c, n, aname, aintval)
#define MTR_SCOPE_I(c, n, aname, aintval)

#define MTR_INSTANT(c, n)
#define MTR_INSTANT_C(c, n, aname, astrval)
#define MTR_INSTANT_I(c, n, aname, aintval)

// Counters (can't do multi-value counters yet)
#define MTR_COUNTER(c, n, val)

// Metadata. Call at the start preferably. Must be const strings.

#define MTR_META_PROCESS_NAME(n)

#define MTR_META_THREAD_NAME(n)
#define MTR_META_THREAD_SORT_INDEX(i)

#endif

// Shortcuts for simple function timing with automatic categories and names.

#define MTR_BEGIN_FUNC() MTR_BEGIN(__FILE__, __FUNCTION__)
#define MTR_END_FUNC() MTR_END(__FILE__, __FUNCTION__)
#define MTR_SCOPE_FUNC() MTR_SCOPE(__FILE__, __FUNCTION__)
#define MTR_INSTANT_FUNC() MTR_INSTANT(__FILE__, __FUNCTION__)
#define MTR_SCOPE_FUNC_LIMIT_S(l) MTRScopedTraceLimit ____mtr_scope(__FILE__, __FUNCTION__, l)
#define MTR_SCOPE_FUNC_LIMIT_MS(l) MTRScopedTraceLimit ____mtr_scope(__FILE__, __FUNCTION__, (double)l * 0.000001)

// Same, but with a single argument of the usual types.
#define MTR_BEGIN_FUNC_S(aname, arg) MTR_BEGIN_S(__FILE__, __FUNCTION__, aname, arg)
#define MTR_END_FUNC_S(aname, arg) MTR_END_S(__FILE__, __FUNCTION__, aname, arg)
#define MTR_SCOPE_FUNC_S(aname, arg) MTR_SCOPE_S(__FILE__, __FUNCTION__, aname, arg)

#define MTR_BEGIN_FUNC_C(aname, arg) MTR_BEGIN_C(__FILE__, __FUNCTION__, aname, arg)
#define MTR_END_FUNC_C(aname, arg) MTR_END_C(__FILE__, __FUNCTION__, aname, arg)
#define MTR_SCOPE_FUNC_C(aname, arg) MTR_SCOPE_C(__FILE__, __FUNCTION__, aname, arg)

#define MTR_BEGIN_FUNC_I(aname, arg) MTR_BEGIN_I(__FILE__, __FUNCTION__, aname, arg)
#define MTR_END_FUNC_I(aname, arg) MTR_END_I(__FILE__, __FUNCTION__, aname, arg)
#define MTR_SCOPE_FUNC_I(aname, arg) MTR_SCOPE_I(__FILE__, __FUNCTION__, aname, arg)

#ifdef __cplusplus
}

#ifdef MTR_ENABLED
// These are optimized to use X events (combined B and E). Much easier to do in C++ than in C.
class MTRScopedTrace {
public:
	MTRScopedTrace(const char *category, const char *name)
		: category_(category), name_(name) {
		start_time_ = mtr_time_s();
	}
	~MTRScopedTrace() {
		internal_mtr_raw_event(category_, name_, 'X', &start_time_);
	}

private:
	const char *category_;
	const char *name_;
	double start_time_;
};

// Only outputs a block if execution time exceeded the limit.
// TODO: This will effectively call mtr_time_s twice at the end, which is bad.
class MTRScopedTraceLimit {
public:
	MTRScopedTraceLimit(const char *category, const char *name, double limit_s)
		: category_(category), name_(name), limit_(limit_s) {
		start_time_ = mtr_time_s();
	}
	~MTRScopedTraceLimit() {
		double end_time = mtr_time_s();
		if (end_time - start_time_ >= limit_) {
			internal_mtr_raw_event(category_, name_, 'X', &start_time_);
		}
	}

private:
	const char *category_;
	const char *name_;
	double start_time_;
	double limit_;
};

class MTRScopedTraceArg {
public:
	MTRScopedTraceArg(const char *category, const char *name, mtr_arg_type arg_type, const char *arg_name, void *arg_value)
		: category_(category), name_(name) {
		internal_mtr_raw_event_arg(category, name, 'B', 0, arg_type, arg_name, arg_value);
	}
	~MTRScopedTraceArg() {
		internal_mtr_raw_event(category_, name_, 'E', 0);
	}

private:
	const char *category_;
	const char *name_;
};
#endif

#endif

#endif
