/* Small portability layer: sleep, tty detection, wall clock. */
#ifndef OPENPHIX_PLATFORM_H
#define OPENPHIX_PLATFORM_H

#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
static inline void obd_sleep_ms(unsigned ms) { Sleep(ms); }
static inline int obd_isatty(FILE *f) { return _isatty(_fileno(f)); }
static inline double obd_now(void) { return (double)GetTickCount64() / 1000.0; }
#else
#include <time.h>
#include <unistd.h>
static inline void obd_sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
static inline int obd_isatty(FILE *f) { return isatty(fileno(f)); }
static inline double obd_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif

#endif
