from utils import *
import json
import os
import re
import sys

def myavg(xs):
    if len(xs) == 0:
        return 0.0
    return sum(xs) / len(xs)

tries = 3
dirs = ['rx']
ncs = [10, 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000]
envs = [('flexnic', False), ('linux', False)]
ncs = [50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 5000, 10000, 20000, 50000, 100000]
outdir = 'experiments/fairness_incast/'

drop_first = 10
drop_last = 5

ds_pat = re.compile(r'bytes=(\d+) conns_open=(\d+) conns_closed=(\d+) '+
        r'conn_bytes_min=(\d+) conn_bytes_max=(\d+) jain_fairness=([0-9\.]+)')

for di in dirs:
  for nc in ncs:
    for (env,ll) in envs:
      tputs = []
      mins = []
      maxs = []
      fairs = []
      for i in range(0, tries):
        fn = '%s/%s_%d_%d_%s_%d.json' % (outdir, di, nc, int(ll), env, i)
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
        tputs += bs
        mins += mis
        maxs += mas
        fairs += fas
      e = env
      if env == 'flexnic':
        if ll:
          e = 'fn_ll'
        else:
          e = 'fn_so'
      print('%s\t%s\t%u\t%d\t%d\t%d\t%f' % (di, e, nc, myavg(tputs), myavg(mins),
          myavg(maxs), myavg(fairs)))
