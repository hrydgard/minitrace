// Minitrace by Henrik Rydgård
// Released under MIT license 2014
//
// Ultra-light dependency free library for performance tracing C/C++ applications.
// Produces traces compatible with Google Chrome's trace viewer.
// Simply open "about:tracing" in Chrome and load the produced JSON.
//
// See README.md for a tutorial.
//
// The trace format is documented here:
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit
// More:
// http://www.altdevblogaday.com/2012/08/21/using-chrometracing-to-view-your-inline-profiling-data/

#include <inttypes.h>

// #define MTR_DISABLED

// By default, will collect up to 100000 events, then you must flush.
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
void mtr_raw_event(const char *category, const char *name, char ph);

#ifndef MTR_DISABLED

// Scopes. In C++, use MTR_SCOPE. In C, always match them within the same scope.
#define MTR_BEGIN(c, n) mtr_raw_event(c, n, 'B')
#define MTR_END(c, n) mtr_raw_event(c, n, 'E')
#define MTR_SCOPE(c, n) MTRScopedTrace ____mtr_scope(c, n)

// Instants.
#define MTR_INSTANT(c, n) mtr_raw_event(c, n, 'I')

#else

#define MTR_BEGIN(c, n)
#define MTR_END(c, n)
#define MTR_SCOPE(c, n)
#define MTR_START(c, n)
#define MTR_FINISH(c, n)
#define MTR_INSTANT(c, n)

#endif

#ifdef __cplusplus
}

class MTRScopedTrace {
public:
  MTRScopedTrace(const char *category, const char *name) : category_(category), name_(name) {
    mtr_raw_event(category, name, 'B');
  }
  ~MTRScopedTrace() {
    mtr_raw_event(category, name, 'E');
  }

private:
  const char *category_;
  const char *name_;
};

#endif
