#include "minitrace.h"
#include <unistd.h>

int main(int argc, const char *argv[]) {
	int i;
	mtr_init("trace.json");

	MTR_BEGIN("main", "outer");
	for (i = 0; i < 10; i++) {
		MTR_BEGIN("main", "scope1");
		usleep(10000);
		MTR_END("main", "scope1");
		usleep(40000);
	}
	MTR_END("main", "outer");

	usleep(100000);
	MTR_INSTANT("main", "the end");

	mtr_flush();
	mtr_shutdown();
	return 0;
}
