#ifndef OPENPHIX_TEST_H
#define OPENPHIX_TEST_H
#include <stdio.h>
#include <stdlib.h>
static int test_failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); test_failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { fprintf(stderr, "%s:%d: %s == %lld, expected %s == %lld\n", __FILE__, __LINE__, #a, _a, #b, _b); test_failures++; } } while (0)
#define TEST_MAIN_END() do { if (test_failures) { fprintf(stderr, "%d failure(s)\n", test_failures); return 1; } puts("ok"); return 0; } while (0)
int hal_stub_load(const char *path);
#endif
