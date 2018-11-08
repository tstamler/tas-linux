import asyncio
import json
import exp_unidir
import os
import sys

sizes = [1, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]
envs = [('linux', True), ('flexnic', True), ('flexnic', False), ('mtcp', True)]
delays = [250, 1000]
dirs = ['rx', 'tx']
tries = 5
fn_threads = 1
max_machines = 4
max_cores = 4
outdir = 'experiments/micro_unidir/'
nc = 64


loop = asyncio.get_event_loop()

for i in range(0, tries):
  for di in dirs:
    for de in delays:
      for sz in sizes:
        for (env,ll) in envs:
          fn = '%s/%s_%d_%d_%d_%s_%d.json' % (outdir, di, de, sz, int(ll), env, i)
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
                  'msg_bytes': sz,
                  'client_lowlevel': ll,
                  'client_machines': num_machines,
                  'client_cores': num_cores,
                  'client_conns': conns_per_thread,
                  'server_cores': 1,
                  'server_mode': di,
                  'server_params': {
                      'op_delay': de,
                      'tx_delay': 10,
                  },
                  'client_params': {
                      'tx_delay': 10,
                  },
                  'time': 60 + 15,
                  'env': {
                      'env': env,
                      'fn_prog': 'fastemu',
                      'fn_threads': fn_threads,
                      'k_extra': [
                          '--cc-dctcp-min=1000',
                          '--cc-rexmit-ints=10',
                      ]

                  }
              }
          if env == 'mtcp':
              params['server_params']['tx_delay'] = 0
              params['client_params']['tx_delay'] = 0

          try:
              res = loop.run_until_complete(exp_unidir.experiment(params))
              with open(fn, 'w+') as f:
                  f.write(json.dumps(res))
          except:
              print(sys.exc_info()[0])

loop.close()
