import asyncio
import json
import exp_rpcubench
import os
import sys

ncs = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]
envs = ['linux', 'flexnic', 'mtcp']
tries = 5
fn_threads = 2
max_machines = 4
max_cores = 4
outdir = 'experiments/urpc/'
msg_bytes = 64
time = 60

loop = asyncio.get_event_loop()

for i in range(0, tries):
  for nc in ncs:
    for env in envs:
      fn = '%s/%d_%s_%d.json' % (outdir, nc, env, i)
      print('\n\n\n################################\n' + fn)
      if os.path.isfile(fn):
          print('Skipping %s because it already exists' % (fn))
          continue

      num_machines = int(min(nc, max_machines))
      num_cores = int(min(nc / num_machines, max_cores))
      conns_per_thread = int(nc / num_machines / num_cores)
      print('conns: %d  machs: %d  cores: %d  cpt: %d' % (nc, num_machines,
          num_cores, conns_per_thread))
      params = {
              'client_machines': num_machines,
              'client_cores': num_cores,
              'client_conns': conns_per_thread,
              'server_cores': 1,
              'server_params': {
                  'msg_bytes': msg_bytes,
              },
              'client_params': {
                  'msg_bytes': msg_bytes,
                  'max_pending': 1,
              },
              'time': time,
              'env': {
                  'env': env,
                  'fn_prog': 'fastemu',
                  'fn_threads': fn_threads,
                  'k_extra': [
                      '--cc-dctcp-min=1000',
                      '--cc-dctcp-step=1000',
                      '--cc-rexmit-ints=10',
                      '--tcp-rxbuf-len=8192',
                      '--tcp-txbuf-len=8192',
                  ]
              }
          }

      #try:
      res = loop.run_until_complete(exp_rpcubench.experiment(params))
      with open(fn, 'w+') as f:
          f.write(json.dumps(res))
      #except:
      #    print(sys.exc_info()[0])

loop.close()
