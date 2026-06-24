#include "util/timer.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace {

int fail(const char* message) {
    std::cerr << "stage2_timer_test: " << message << '\n';
    return 1;
}

int test_timer_reports_elapsed_time() {
    asyncdataloader::util::Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    if (timer.elapsed_ns() == 0) {
        return fail("elapsed_ns should increase after time passes");
    }
    if (timer.elapsed_ms() <= 0.0) {
        return fail("elapsed_ms should increase after time passes");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int result = test_timer_reports_elapsed_time(); result != 0) {
        return result;
    }

    return 0;
}
