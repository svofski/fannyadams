#pragma once

#ifdef TEST
#include <stdio.h>
#include <sys/stat.h>

void gnuplot_plot_headers(FILE * fo, const char * title, const char * legend,
        const char * using, const char * xlabel, const char * ylabel);
FILE * fopen_exe(const char * name);

#endif
