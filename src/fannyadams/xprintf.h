#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int xprintf(const char *format, ...);
int xsprintf(char *out, const char *format, ...);
extern int xputchar(int c);

#ifdef __cplusplus
}
#endif

