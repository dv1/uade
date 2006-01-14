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
    return 0.42 - 0.5*math.cos((x+0.5) * math.pi * 2) + 0.08*math.cos((x+0.5) * math.pi * 4);

sinwin = blackman

def sinc(x):
    if x == 0:
        return 1;
    return math.sin(x * math.pi) / (x * math.pi)

convolution_size = 4096
sampling_freq = 3540000
stopfreq = 21000
ratio = sampling_freq / stopfreq / 2.0

def make_table():
    acc = 0.0;
    for _ in range(convolution_size):
        x = _ - convolution_size/2
        newval1  = sinc( x        / ratio) * sinwin( x        / float(convolution_size))
        newval15 = sinc((x + 0.5) / ratio) * sinwin((x + 0.5) / float(convolution_size))
        newval2  = sinc((x + 1  ) / ratio) * sinwin((x + 1  ) / float(convolution_size))
        acc += (newval1 + 2*newval15 + newval2) / 4 / ratio;
        print ("%5d," % (acc * 65536)),
        if _ % 12 == 11:
            print

def make_spectrum():
    acc = []
    for _ in range(convolution_size):
        x = _ - convolution_size/2
        acc.append(sinc(x / ratio) * sinwin(x / float(convolution_size)))

    acc = abs(fft(acc))
    for x in range(0, convolution_size/2):
        print "%d %f" % (x / float(convolution_size) * sampling_freq, acc[x])

make_table()
#make_spectrum()
