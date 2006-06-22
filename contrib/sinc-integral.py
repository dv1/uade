#!/usr/bin/python

import math
import sys
from scipy import array, exp
from scipy.signal import firwin
from scipy.fftpack import fft, ifft

def fir_minphase(table, pad_size=4):
    table = list(table)
    # table should be a real-valued table of FIR coefficients
    convolution_size = len(table)
    table += [0] * (convolution_size * (pad_size - 1))

    # compute the real cepstrum
    # fft -> abs + ln -> ifft -> real
    cepstrum = ifft(map(lambda x: math.log(x), abs(fft(table))))
    # because the positive and negative freqs were equal, imaginary content is neglible
    # cepstrum = map(lambda x: x.real, cepstrum)

    # window the cepstrum in such a way that anticausal components become rejected
    cepstrum[1                :len(cepstrum)/2] *= 2;
    cepstrum[len(cepstrum)/2+1:len(cepstrum)  ] *= 0;

    # now cancel the previous steps:
    # fft -> exp -> ifft -> real
    cepstrum = ifft(map(exp, fft(cepstrum)))
    return map(lambda x: x.real, cepstrum[0:convolution_size])

class BiquadFilter(object):
    __slots__ = ['b0', 'b1', 'b2', 'a1', 'a2', 'x1', 'x2', 'y1', 'y2']

    def __init__(self, b0, b1, b2, a1, a2):
        self.b0 = b0
        self.b1 = b1
        self.b2 = b2
        self.a1 = a1
        self.a2 = a2
        self.reset()

    def reset(self):
        self.x1 = 0.0
        self.x2 = 0.0
        self.y1 = 0.0
        self.y2 = 0.0

    def filter(self, x0):
        y0 = self.b0*x0 + self.b1*self.x1 + self.b2*self.x2 - self.a1*self.y1 - self.a2*self.y2
        self.x2 = self.x1
        self.x1 = x0

        self.y2 = self.y1
        self.y1 = y0

        return y0

def make_rc_lopass(sample_rate, freq):
    omega = 2 * math.pi * freq / sample_rate;
    term = 1 + 1/omega;
    return BiquadFilter(1/term, 0.0, 0.0, -1.0 + 1/term, 0.0);

def make_butterworth(fs, fc):
    omega = 2 * math.pi * fc / fs;
    c = 1 + 2 * math.cos(math.pi / 4) * omega + omega ** 2
    a0 = a2 = omega ** 2 / c
    a1 = 2 * a0
    b1 = 2 * (omega ** 2 - 1) / c
    b2 = (1 - 2 * math.cos(math.pi / 4) * omega + omega ** 2) / c
    return BiquadFilter(a0, a1, a2, b1, b2)

def quantize(x, bits, scale=False):
    x = list(x)
    fact = 2 ** bits

    # this adjusts range precisely between -65536 and 0 so that our bleps look right
    correction_factor = x[-1] - x[0]

    for _ in range(len(x)):
        val = x[_] * fact / correction_factor;
        # correct rounding
        if val < 0:
            val = int(val - 0.5)
        else:
            val = int(val + 0.5)
        # leave scaled?
        if not scale:
            val /= float(fact)
        x[_] = val * -1
    return x

def lin2db(lin):
    return 20 * (math.log(lin) / math.log(10))

def print_spectrum(table, sample_rate, full=False):
    fact = 2
    if full:
        fact = 1

    for _ in range(len(table) / fact):
        mag = lin2db(abs(table[_]))
        pha = math.atan2(table[_].real, table[_].imag)
        print "%s %s %s" % (float(_) / len(table) * sample_rate, mag, pha)

def print_fir(table, format='gnuplot'):
    if format == 'gnuplot':
        for _ in range(len(table)):
            print "%s %s" % (_, table[_])
    elif format == 'c':
        for _ in range(len(table)):
            print ("%6s," % table[_]),
            if _ % 11 == 10:
                print

def integrate(table):
    total = 0
    for _ in table:
        total += _
    startval = -total
    new = []
    for _ in table:
        startval += _
        new.append(startval)
    
    return new

def run_filter(flt, table):
    flt.reset()

    # initialize filter to stable state
    for _ in range(10000):
        flt.filter(table[0])

    # now run the filter
    newtable = []
    for _ in range(len(table)):
        newtable.append(flt.filter(table[_]))

    return newtable

AMIGA_PAL_CLOCK = 3546895
def main(spectrum=True):
    spectrum = len(sys.argv) > 1

    unfiltered = firwin(2048, 21000.0 / AMIGA_PAL_CLOCK * 2, window=('kaiser', 4.0))
    # move filtering effects to start to allow IIRs more time to settle
    unfiltered = fir_minphase(unfiltered)

    # make digital models for the filters on Amiga 500 and 1200.
    filter_fixed6khz = make_rc_lopass(AMIGA_PAL_CLOCK, 4900.0)
    filter_leakage = make_rc_lopass(AMIGA_PAL_CLOCK, 32000.0)
    filter_led = make_butterworth(AMIGA_PAL_CLOCK, 1600.0)

    # produce the filtered outputs
    amiga500_off = run_filter(filter_fixed6khz, unfiltered)
    amiga1200_off = run_filter(filter_leakage, unfiltered)
    amiga500_on = run_filter(filter_led, amiga500_off)
    amiga1200_on = run_filter(filter_led, unfiltered)

    if not spectrum:
        # integrate to produce blep
        amiga500_off = integrate(amiga500_off)
        amiga500_on = integrate(amiga500_on)
        amiga1200_off = integrate(amiga1200_off)
        amiga1200_on = integrate(amiga1200_on)
    
    # quantize and scale
    amiga500_off = quantize(amiga500_off, bits=17, scale=(not spectrum))
    amiga500_on = quantize(amiga500_on, bits=17, scale=(not spectrum))
    amiga1200_off = quantize(amiga1200_off, bits=17, scale=(not spectrum))
    amiga1200_on = quantize(amiga1200_on, bits=17, scale=(not spectrum))

    if spectrum:
        spec = int(sys.argv[1])
        if spec == -1:
            table = unfiltered
        if spec == 0:
            table = amiga500_off
        if spec == 1:
            table = amiga500_on
        if spec == 2:
            table = amiga1200_off
        if spec == 3:
            table = amiga1200_on

        print_spectrum(fft(table), sample_rate=AMIGA_PAL_CLOCK)
        else:
        print " /*"
        print "  * Table generated by contrib/sinc-integral.py."
        print "  */"
        print
        print '#include "sinctable.h"'
            print
        print "const int winsinc_integral[4][%d] = {" % len(unfiltered)
        print "   {"
        print_fir(amiga500_off, format='c')
        print "    }, {"
        print_fir(amiga500_on, format='c')
        print "    }, {"
        print_fir(amiga1200_off, format='c')
        print "    }, {"
        print_fir(amiga1200_on, format='c')
        print "    }"
        print "};"

if __name__ == '__main__':
    main()

