from utils import *
import json
import os
import re
import sys

def myavg(xs):
    if len(xs) == 0:
        return -1
    return sum(xs) / len(xs)

def mymin(xs):
    if len(xs) == 0:
        return -1
    return min(xs)

def mymax(xs):
    if len(xs) == 0:
        return -1
    return max(xs)

def myxavg(xs):
    tot = 0.0
    n = 0
    for x in xs:
        if x is None:
            return None
        tot += x
        n += 1

    if n > 0:
        tot /= n
    return tot

def myxint(xs):
    x = int(xs)
    if x == -1:
        return None
    return x

def xtos(x):
    if x is None:
        return '-1'
    return str(x)

ncs = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]
envs = ['linux', 'flexnic', 'mtcp']
tries = 5
msg_bytes = 64
outdir = 'experiments/urpc/'

drop_first = 10
drop_last = 10

ds_pat = re.compile(r'TP: total=([0-9\.,]+) mbps  50p=(-?[0-9,]+) us  ' +
        r'90p=(-?[0-9,]+) us  95p=(-?[0-9,]+) us  99p=(-?[0-9,]+) us  ' +
        r'99\.9p=(-?[0-9,]+) us  99.99p=(-?[0-9,]+) us.*')

for env in envs:
  for nc in ncs:
    tputs, p50s, p90s, p95s, p99s, p999s, p9999s = [], [], [], [], [], [], []
    for i in range(0, tries):
      fn = '%s/%d_%s_%d.json' % (outdir, nc, env, i)
      if not os.path.isfile(fn):
          continue
      with open(fn, 'r') as f:
          data = json.load(f)

      ctps, cp50s, cp90s, cp95s, cp99s, cp999s, cp9999s = \
          [], [], [], [], [], [], []
      for c in data['clients']:
        tp, p50, p90, p95, p99, p999, p9999 = [], [], [], [], [], [], []
        for l in c['stdout']:
          res = ds_pat.match(l)
          if not res:
              continue

          tp.append(float(res.group(1)))
          p50.append(myxint(res.group(2)))
          p90.append(myxint(res.group(3)))
          p95.append(myxint(res.group(4)))
          p99.append(myxint(res.group(5)))
          p999.append(myxint(res.group(6)))
          p9999.append(myxint(res.group(7)))

        # drop prefix and suffix
        tp = tp[drop_first:][:-drop_last]
        p50 = p50[drop_first:][:-drop_last]
        p90 = p90[drop_first:][:-drop_last]
        p95 = p95[drop_first:][:-drop_last]
        p99 = p99[drop_first:][:-drop_last]
        p999 = p999[drop_first:][:-drop_last]
        p9999 = p9999[drop_first:][:-drop_last]

        ctps.append(myavg(tp))
        cp50s += p50
        cp90s += p90
        cp95s += p95
        cp99s += p99
        cp999s += p999
        cp9999s += p9999

      tputs.append(sum(ctps) / 8 / msg_bytes)
      p50s.append(myxavg(cp50s))
      p90s.append(myxavg(cp90s))
      p95s.append(myxavg(cp95s))
      p99s.append(myxavg(cp99s))
      p999s.append(myxavg(cp999s))
      p9999s.append(myxavg(cp9999s))

    print('%s\t%d\t%f\t%f\t%f\t%s\t%s\t%s\t%s\t%s\t%s' % (env, nc, myavg(tputs),
      mymin(tputs), mymax(tputs), xtos(myxavg(p50s)), xtos(myxavg(p90s)),
      xtos(myxavg(p95s)), xtos(myxavg(p99s)), xtos(myxavg(p999s)),
      xtos(myxavg(p9999))))
