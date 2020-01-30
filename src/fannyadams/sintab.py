#!/usr/bin/env python3

from math import sin,pi,log

scale = 16384
period=256
s = [str(round(sin(x/period * 2 * pi) * scale)) for x in range(period)]
print(f"const int16_t sintab[{len(s)}]={{{','.join(s)}}};")
print(f"#define sintab_mask {period-1}")
