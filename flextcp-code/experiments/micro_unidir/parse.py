from utils import *
import json
import os
import re
import sys
import statistics

def myavg(xs):
    if len(xs) == 0:
        return 0.0
    return sum(xs) / len(xs)

def mymin(xs):
    if len(xs) == 0:
        return 0.0
    return min(xs)

def mymax(xs):
    if len(xs) == 0:
        return 0.0
    return max(xs)

def mymedian(xs):
    if len(xs) == 0:
        return 0.0
    return statistics.median(xs)


sizes = [1, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]
envs = [('linux', True), ('flexnic', True), ('flexnic', False), ('mtcp', True)]
delays = [250, 1000]
dirs = ['rx', 'tx']
tries = 5
outdir = 'experiments/micro_unidir/'

drop_first = 10
drop_last = 5

ds_pat = re.compile(r'bytes=(\d+) conns_open=(\d+) conns_closed=(\d+) '+
        r'conn_bytes_min=(\d+) conn_bytes_max=(\d+) jain_fairness=([0-9\.]+)')

for di in dirs:
  for de in delays:
    for (env,ll) in envs:
      for sz in sizes:
        tputs = []
        mins = []
        maxs = []
        fairs = []
        for i in range(0, tries):
          fn = '%s/%s_%d_%d_%d_%s_%d.json' % (outdir, di, de, sz, int(ll), env, i)
          if not os.path.isfile(fn):
              continue
          with open(fn, 'r') as f:
              data = json.load(f)

          bs = []
          mis = []
          mas = []
          fas = []
          for l in data['server']['stdout']:
              res = ds_pat.match(l)
              if not res:
                  continue

              bs.append(float(res.group(1)))
              mis.append(float(res.group(4)))
              mas.append(float(res.group(5)))
              fas.append(float(res.group(6)))
          bs = bs[drop_first:]
          mis = mis[drop_first:]
          mas = mas[drop_first:]
          fas = fas[drop_first:]
          bs = bs[:-drop_last]
          mis = mis[:-drop_last]
          mas = mas[:-drop_last]
          fas = fas[:-drop_last]
          tputs.append(myavg(bs))
          mins += mis
          maxs += mas
          fairs += fas
        e = env
        if env == 'flexnic':
          if ll:
            e = 'fn_ll'
          else:
            e = 'fn_so'
        print('%s\t%d\t%s\t%u\t%d\t%d\t%d' % (di, de, e, sz,
            mymedian(tputs), mymin(tputs), mymax(tputs)))
