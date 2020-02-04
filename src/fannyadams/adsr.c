#include "adsr.h"


void adsr_reset(adsr_t * env, float a, float d, float s, float r)
{
    env->a = a;
    env->d = d;
    env->s = s;
    env->r = r;
    env->v = 0;
    env->state = ADSR_0;
}

void adsr_note_on(adsr_t * env)
{
    if (env->state == ADSR_0) {
        env->v = 0;
    }
    env->state = ADSR_A;
}

void adsr_note_off(adsr_t * env)
{
    env->state = ADSR_R;
}

void adsr_step(adsr_t * env)
{
    switch (env->state) {
        case ADSR_A:
            env->v += env->a;
            if (env->v >= 1) {
                env->v = 1;
                env->state = ADSR_D;
            }
            break;
        case ADSR_D:
            env->v -= env->d;
            if (env->v <= env->s) {
                env->v = env->s;
                env->state = ADSR_S;
            }
            break;
        case ADSR_S:
            break;
        case ADSR_R:
            env->v -= env->r;
            if (env->v <= 0) {
                env->v = 0;
                env->state = ADSR_0;
            }
            break;
        case ADSR_0:
        default:
            break;
    }
}

#ifdef TEST
#include <stdio.h>
#include <stdlib.h>
#include "test/test.h"

int adsr_test()
{
    adsr_t env;
    adsr_reset(&env, 0.01, 0.01, 0.2, 0.01);

    FILE * fo = fopen_exe("adsr_test1.txt");
    gnuplot_plot_headers(fo, "adsr\\_test(): test envelope generator",
            "ADSR", "1:2", "sample", "volume");
    for (int step = 0; step < 500; ++step) {
        if (step == 10) adsr_note_on(&env);
        if (step == 300) adsr_note_off(&env);
        adsr_step(&env);
        fprintf(fo, "%d %1.4f # %d\n", step, env.v, env.state);
    }
    fprintf(fo, "e\n#pause -1\n");
    fclose(fo);
    system("./adsr_test1.txt");

    return 0;
}
#endif
