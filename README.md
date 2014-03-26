minitrace
=========
by Henrik Rydgård 2014

Simple C/C++ library for producing JSON traces suitable for Chrome's built-in excellent trace viewer (about:tracing).

Extremely simple to use.

  1. Include minitrace.c and minitrace.h in your project. #include minitrace.h in some common header.

  2. In your initialization code:

        mtr_init("trace.json");

  3. In your exit code:

        mtr_shutdown();

  4. In all functions you want to profile:

        // C
        MTR_BEGIN("GFX", "RasterizeTriangle")
        ...
        MTR_END("GFX", "RasterizeTriangle")

        // C++
        MTR_SCOPE("GFX", "RasterizeTriangle")

  5. In Google Chrome open "about:tracing"

  6. Click Open, and choose your trace.json

  7. Navigate the trace view using the WASD keys, and Look for bottlenecks and optimize your application. 

  8. In your final release build, build with

         -DMTR_DISABLE


By default, it will collect 1 million tracepoints and then stop. You can change this behaviour, see the
top of the header file.

Note: Please only use string literals in MTR statements.

Here's an example:


    int main(int argc, const char *argv[]) {
      int i;
      mtr_init("trace.json");

      MTR_META_PROCESS_NAME("minitrace_test");
      MTR_META_THREAD_NAME("main thread");

      int long_running_thing_1;
      int long_running_thing_2;

      MTR_START("background", "long_running", &long_running_thing_1);
      MTR_START("background", "long_running", &long_running_thing_2);

      MTR_BEGIN("main", "outer");
      usleep(80000);
      for (i = 0; i < 3; i++) {
        MTR_BEGIN("main", "inner");
        usleep(40000);
        MTR_END("main", "inner");
        usleep(10000);
      }
      MTR_STEP("background", "long_running", &long_running_thing_1, "middle step");
      usleep(80000);
      MTR_END("main", "outer");

      usleep(50000);
      MTR_INSTANT("main", "the end");
      usleep(10000);
      MTR_FINISH("background", "long_running", &long_running_thing_1);
      MTR_FINISH("background", "long_running", &long_running_thing_2);

      mtr_flush();
      mtr_shutdown();
      return 0;
    }

It will result in something looking a little like this:

![minitrace](http://www.ppsspp.org/img/minitrace.png)

Future plans:

  * Builtin background flush thread support with better synchronization, no more fixed limit
  * Support for trace arguments
