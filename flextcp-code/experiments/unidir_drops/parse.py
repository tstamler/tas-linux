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
dfs = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.0]
envs = ['flexnic', 'linux']
outdir = 'experiments/unidir_drops/'

drop_first = 20
drop_last = 10

ds_pat = re.compile(r'bytes=(\d+) conns_open=(\d+) conns_closed=(\d+) '+
        r'conn_bytes_min=(\d+) conn_bytes_max=(\d+) jain_fairness=([0-9\.]+)')

for df in dfs:
  for env in envs:
    tputs = []
    mins = []
    maxs = []
    fairs = []
    for i in range(0, tries):
      fn = '%s/%f_%s_%d.json' % (outdir, df, env, i)
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
    print('%s\t%f\t%d\t%d\t%d\t%f' % (env, df, myavg(tputs), myavg(mins),
        myavg(maxs), myavg(fairs)))
