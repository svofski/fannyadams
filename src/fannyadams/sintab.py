#!/usr/bin/env python3

from math import sin,pi,log

scale = 1.0

valuetype="float"
precision=32 # phase accumulator precision, 32 for uint_32t
fix_mant=20  # mantissa bits
fix_int=precision - fix_mant    # integral part bits
period=(1 << fix_int)

s = [str((sin(x/period * 2 * pi) * scale)) for x in range(period)]
print(f"const {valuetype} sintab[{len(s)}]={{{','.join(s)}}};")
print(f"#define sintab_mask {period-1}")
print(f"#define FIX {fix_mant}")
