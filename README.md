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

Future plans:

  * Builtin background flush thread support with better synchronization, no more fixed limit
  * Support for trace arguments
