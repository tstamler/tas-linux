import asyncio
import json
import exp_unidir
import os
import sys

tries = 5
fn_threads = 1
max_cores = 4
outdir = 'experiments/compatibility/'
envs = ['flexnic', 'linux']
nc = 100
di = 'rx'
sz = 2048

totalmem = (1024+256)*1024*1024
tx_del = 5
run_time = 60

loop = asyncio.get_event_loop()

for i in range(0, tries):
  for env_s in envs:
    for env_cs in envs:
      fn = '%s/%s_%s_%d.json' % (outdir, env_s, env_cs, i)
      print('\n\n\n################################\n' + fn)
      if os.path.isfile(fn):
          print('Skipping %s because it already exists' % (fn))
          continue

      bufsz = 1 << (int(totalmem / nc / 2).bit_length() - 1)

      num_cores = int(min(nc, max_cores))
      conns_per_thread = int(nc / num_cores)

      params = {
              'msg_bytes': sz,
              'client_lowlevel': False,
              'client_machines': 1,
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
              'time': run_time + tx_del,
              'env': {
                  'fn_prog': 'fastemu',
                  'fn_threads': fn_threads,
                  'k_extra': [
                      '--tcp-rxbuf-len=' + str(bufsz),
                      '--tcp-txbuf-len=' + str(bufsz),
                      '--cc-dctcp-min=1000',
                      '--cc-rexmit-ints=10',
                  ]
              },
              'env_server': {
                  'env': env_s,
              },
              'env_clients': {
                  'env': env_cs,
              },
          }

      #try:
      res = loop.run_until_complete(exp_unidir.experiment(params))
      with open(fn, 'w+') as f:
          f.write(json.dumps(res))
      #except:
      #    print(sys.exc_info()[0])

loop.close()
