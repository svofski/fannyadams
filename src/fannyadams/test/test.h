#pragma once

#ifdef TEST
#include <stdio.h>
#include <sys/stat.h>

void gnuplot_plot_headers(FILE * fo, const char * title, const char * legend,
        const char * using, const char * xlabel, const char * ylabel);

void gnuplot_plot_headers4(FILE* fo,
                           const char* title,
                           const char* xlabel,
                           const char* ylabel);

void gnuplot_plot4(FILE * fo, const char * legend[]);

FILE * fopen_exe(const char * name);

#endif
