#include "adsr.h"


void adsr_reset(adsr_t * env, float a, float d, float s, float r)
{
    env->a = a;
    env->d = d;
    env->s = s;
    env->r = r;
    env->v = 0;
    env->state = ADSR_A;
}

void adsr_step(adsr_t * env, int note_on)
{
    if (!note_on) {
        env->state = ADSR_R;
    }

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
            if (env->v < 0) {
                env->v = 0;
            }
            break;
    }
}

#ifdef TEST
#include <stdio.h>

int adsr_test()
{
    adsr_t env;
    adsr_reset(&env, 0.01, 0.01, 0.2, 0.01);

    FILE * fo = fopen("test_adsr_1.txt", "w");
    fprintf(fo, "#!/usr/bin/env gnuplot\n");
    fprintf(fo, "# adsr,v=%f %f %f %f %f\n", env.a, env.d, env.s, env.r, env.v);
    fprintf(fo, "plot '-' using 1:2 with lines\n");
    for (int step = 0; step < 500; ++step) {
        adsr_step(&env, step < 300);
        fprintf(fo, "%d %1.4f # %d\n", step, env.v, env.state);
    }
    fprintf(fo, "e\npause -1\n");
    fclose(fo);

    return 0;
}
#endif
