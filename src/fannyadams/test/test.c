#include <stdio.h>
#include <sys/stat.h>

#include "midi.h"

void gnuplot_plot_headers(FILE * fo, const char * title, const char * legend, 
        const char * using, const char * xlabel, const char * ylabel)
{
    fprintf(fo, "#!/usr/bin/env gnuplot\n");

    fprintf(fo,
            "set title '%s' textcolor rgb 'white'\n"
            "set xlabel '%s' textcolor rgb 'white'\n"
            "set ylabel '%s' textcolor rgb 'white'\n"
            "set linetype 1 lc rgb 'orange'\n"
            "set linetype 11 lc rgb 'white'\n"
            "set border lc 11\n"
            "set key textcolor rgb 'white'\n"
            "plot '-' using %s with lines title '%s'\n",
            title,
            xlabel ? xlabel : "x",
            ylabel ? ylabel : "y",
            using ? using : "1:2",
            legend ? legend : "Epic plot");
}

void gnuplot_plot_headers4(FILE * fo, 
        const char * title, const char * xlabel, const char * ylabel)
{
    fprintf(fo, "#!/usr/bin/env gnuplot\n");

    fprintf(fo,
            "set title '%s' textcolor rgb 'white'\n"
            "set xlabel '%s' textcolor rgb 'white'\n"
            "set ylabel '%s' textcolor rgb 'white'\n"
            "set linetype 1 lc rgb 'orange'\n"
            "set linetype 11 lc rgb 'white'\n"
            "set border lc 11\n"
            "set key textcolor rgb 'white'\n",
            title,
            xlabel ? xlabel : "x",
            ylabel ? ylabel : "y");
}

void gnuplot_plot4(FILE * fo, const char * legend[])
{
    fprintf(fo, "plot $data using 1:2 with lines title '%s',"
            "'' using 1:3 with lines title '%s',"
            "'' using 1:4 with lines title '%s',"
            "'' using 1:5 with lines title '%s'\n",
            legend[0], legend[1], legend[2], legend[3]);
}

FILE * fopen_exe(const char * name)
{
    FILE * fo = fopen(name, "w");
    chmod(name, 0744);
    return fo;
}

int main()
{
    extern int event_test();
    extern int adsr_test();
    extern int osc_test();
    extern int voice_test();
    extern int keypress_test();

    int res = 0;
    
    //res |= event_test();
    //res |= adsr_test();
    //res |= osc_test();
    //res |= voice_test();
    res |= keypress_test();
    
    return res;
}
