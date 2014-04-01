// minitrace
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

#include <inttypes.h>

// #define MTR_ENABLED

// By default, will collect up to 1000000 events, then you must flush.
// It's recommended that you simply call mtr_flush on a background thread
// occasionally. It's safe...ish.
#define BUFFER_SIZE 1000000

#ifdef __cplusplus
extern "C" {
#endif

// C API
void mtr_init(const char *json_file);
void mtr_shutdown();

void mtr_start();
void mtr_stop();

void mtr_flush();
double mtr_time_s();

// Note: Does nothing on Win32 yet
void mtr_register_sigint_handler();

// If str is semi dynamic, store it permanently in a pool so we don't need to malloc it.
// Not exactly fast though, should use a hash table or something.
const char *mtr_pool_string(const char *str);

// Commented-out types will be supported in the future.
typedef enum {
	MTR_ARG_TYPE_NONE = 0,
	MTR_ARG_TYPE_INT = 1,	// I
	// MTR_ARG_TYPE_FLOAT = 2,
	// MTR_ARG_TYPE_DOUBLE = 3,
	MTR_ARG_TYPE_STRING_CONST = 8,	// C
	MTR_ARG_TYPE_STRING_COPY = 9,
	// MTR_ARG_TYPE_JSON_COPY = 10,
} mtr_arg_type;

// TODO: Add support for more than one argument (metadat) per event
// Having more costs speed and memory.
#define MTR_MAX_ARGS 1

// Only use the macros to call these.
void internal_mtr_raw_event(const char *category, const char *name, char ph, void *id);
void internal_mtr_raw_event_arg(const char *category, const char *name, char ph, void *id, mtr_arg_type arg_type, const char *arg_name, void *arg_value);

#ifdef MTR_ENABLED

// Scopes. In C++, use MTR_SCOPE. In C, always match them within the same scope.
#define MTR_BEGIN(c, n) internal_mtr_raw_event(c, n, 'B', 0)
#define MTR_END(c, n) internal_mtr_raw_event(c, n, 'E', 0)
#define MTR_SCOPE(c, n) MTRScopedTrace ____mtr_scope(c, n)
#define MTR_SCOPE_LIMIT(c, n, l) MTRScopedTraceLimit ____mtr_scope(c, n, l)

#define MTR_START(c, n, id) internal_mtr_raw_event(c, n, 'S', (void *)(id))
#define MTR_STEP(c, n, id, step) internal_mtr_raw_event_arg(c, n, 'T', (void *)(id), MTR_ARG_TYPE_STRING_CONST, "step", (void *)(step))
#define MTR_FINISH(c, n, id) internal_mtr_raw_event(c, n, 'F', (void *)(id))


// BEGIN/END with a single named argument. _C is for a const string arg, _I for int. _S will copy the string, which is expensive
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

#define MTR_INSTANT(c, n) internal_mtr_raw_event(c, n, 'I', 0)
#define MTR_INSTANT_C(c, n, aname, astrval) internal_mtr_raw_event(c, n, 'I', 0, MTR_ARG_TYPE_STRING_CONST, aname, (void *)(astrval))
#define MTR_INSTANT_I(c, n, aname, aintval) internal_mtr_raw_event(c, n, 'I', 0, MTR_ARG_TYPE_INT, aname, (void *)(aintval))

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
