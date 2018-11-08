import asyncio
import json
import exp_flexkvs
import os
import sys

cores = [1, 2, 4]
envs = ['flexnic', 'linux', 'mtcp']
tries = 5
fnts = [2, 1, 4]
fn_threads = 2
max_machines = 4
max_cores = 4
outdir = 'experiments/flexkvs/'
warmup_time = 10
cooldown_time = 10
time = 50
nc = 64

loop = asyncio.get_event_loop()

for i in range(0, tries):
  for fnt in fnts:
    for sc in cores:
      for env in envs:
        if env != 'flexnic' and fnt != 1:
            continue
        fn = '%s/%d_%s_%d_%d.json' % (outdir, sc, env, fnt, i)
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
                'server_cores': sc,
                'server_params': {
                },
                'client_params': {
                    'warmup': warmup_time,
                    'cooldown': cooldown_time,
                    'time': time,
                    'extra': ['-z', '.9', '-v', '64', '-n', '100000'],
                },
                'time': warmup_time + time + cooldown_time + 5,
                'env': {
                    'env': env,
                    'fn_prog': 'fastemu',
                    'fn_threads': fnt,
                    'k_extra': [
                        #'--cc=const-rate',
                        '--cc-const-rate=100000',
                        '--cc-dctcp-min=10000',
                        '--cc-dctcp-step=10000',
                        '--cc-rexmit-ints=10',
                        '--tcp-rxbuf-len=8192',
                        '--tcp-txbuf-len=8192',
                    ]
                }
            }

        #try:
        res = loop.run_until_complete(exp_flexkvs.experiment(params))
        with open(fn, 'w+') as f:
            f.write(json.dumps(res))
        #except:
        #    print(sys.exc_info()[0])

loop.close()
