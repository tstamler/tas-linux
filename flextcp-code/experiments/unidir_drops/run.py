import asyncio
import json
import exp_unidir
import os
import sys

tries = 5
fn_threads = 1
max_machines = 1
max_cores = 4
outdir = 'experiments/unidir_drops/'
envs = ['flexnic', 'linux']
nc = 100
di = 'rx'
sz = 2048
dfs = [0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.0]

totalmem = (1024+256)*1024*1024
tx_del = 5
run_time = 60

envs = ['flexnic', 'linux']

loop = asyncio.get_event_loop()

for i in range(0, tries):
  for df in dfs:
    for env in envs:
      fn = '%s/%f_%s_%d.json' % (outdir, df, env, i)
      print('\n\n\n################################\n' + fn)
      if os.path.isfile(fn):
          print('Skipping %s because it already exists' % (fn))
          continue

      bufsz = 1 << (int(totalmem / nc / 2).bit_length() - 1)

      num_machines = int(min(nc, max_machines))
      num_cores = int(min(nc / num_machines, max_cores))
      conns_per_thread = int(nc / num_machines / num_cores)

      params = {
              'msg_bytes': sz,
              'drops': df,
              'client_lowlevel': False,
              'client_machines': num_machines,
              'client_cores': num_cores,
              'client_conns': conns_per_thread,
              'server_cores': min(nc, max_cores),
              'server_mode': di,
              'server_params': {
                  'tx_delay': tx_del,
              },
              'client_params': {
                  'tx_delay': tx_del,
              },
              'router_params': [
              ],
              'time': run_time + tx_del,
              'env': {
                  'env': env,
                  'fn_prog': 'fastemu',
                  'fn_threads': fn_threads,
                  'k_extra': [
                      '--tcp-rxbuf-len=' + str(bufsz),
                      '--tcp-txbuf-len=' + str(bufsz),
                      '--cc-dctcp-min=1000',
                      '--cc-rexmit-ints=10',
                  ]
              }
          }

      #try:
      res = loop.run_until_complete(exp_unidir.experiment(params))
      with open(fn, 'w+') as f:
          f.write(json.dumps(res))
      #except:
      #    print(sys.exc_info()[0])

loop.close()
