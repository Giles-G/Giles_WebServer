#include "../Log/log.hpp"
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

int64_t get_current_millis(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char** argv)
{
    LOG_INIT("log", "myname", 3);
    uint64_t start_ts = get_current_millis();
    for (int i = 0;i < 10; ++i)
    {
        sleep(1);
        LOG_ERROR("%s", "my number is number my number is my number is my number is my number is my number is my number is");
    }
    uint64_t end_ts = get_current_millis();
    printf("time use %lums\n", end_ts - start_ts);
}
