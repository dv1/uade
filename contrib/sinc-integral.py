#!/usr/bin/python

# the idea is to:
#
# - choose a length of the sinc to compute. I think a sufficient sinc would be 1024 point
#   sinc
#
# - choose the sinc queue depth. We can probably assume that periods up to 100 are
#   maximum, so we choose 10 as the queue depth.
#
# - we store in the sinc queue structures with two bits of information:
#   - how long time ago this value was set (relative to position in sinc convolution,
#     from 0 to 1024)
#   - volume * data
#
# - we also need to store the last value which fell out of the sinc queue somewhere
#
# - when a sample is requested, we shall compute the sinc integral of the data in queue
#   based on 1024 tabularized sinc integral values and the queue contents.

# Let's assume that the data points are something like this:
#
# (50, 100)
# (480, 200)
# (1000, 500)
# lastvalue = 300

# In order to compute the integral we shall compute the following:
# (integral(0) - integral(50)) * 100
# (integral(50) - integral(480)) * 200
# (integral(480) - integral(1000)) * 500
# (integral(1000) - integral(1024)) * 300

import math
from scipy.fftpack import fft

# the raised cosine window, -0.5 to 0.5
def hanning(x):
    return 0.50 + 0.50*math.cos(x * math.pi * 2)

def blackman(x):
    return 0.42-0.5*math.cos((x+0.5) * math.pi * 2)+0.08*math.cos((x+0.5) * math.pi * 4)

sinwin = blackman
#sinwin=hanning

def sinc(x):
    if x == 0:
        return 1;
    return math.sin(x * math.pi) / (x * math.pi)

convolution_size = 4096
sampling_freq = 3541200
stopfreq = 20000
valuerange = 65536
ratio = float(sampling_freq) / stopfreq / 2

def make_table():
    table = []
    acc = -0.5
    for _ in range(convolution_size):
        x = _ - convolution_size/2
        acc += sinc(x / ratio) * sinwin(x / float(convolution_size)) / ratio
        aprx = acc * valuerange * 2
        if aprx < 0:
            aprx = int(aprx - 0.5)
        else:
            aprx = int(aprx + 0.5)
        table.append(aprx)
    return table

def make_spectrum():
    table = make_table();
    acc = []
    oldx = table[0]
    for x in table:
        acc.append(float(oldx - x) / (table[-1] - table[0]))
        oldx = x

    acc = abs(fft(acc))**2
    for x in range(0, convolution_size/2):
        print "%f %f" % (x / float(convolution_size) * sampling_freq, math.log(acc[x]) / math.log(10) * 10)

def print_table(table):
    _ = 0
    for x in table:
        print ("%6d," % x),
        if _ % 10 == 9:
            print
        _ += 1

print_table(make_table())
#make_spectrum()
