import asyncio
import json
import exp_unidir
import os
import sys

envs = [('linux', True), ('flexnic', True)]
ncs = [50000, 10000, 5000, 1000, 500, 100]
dirs = ['rx', 'tx']
tries = 3
fn_threads = 1
max_machines = 4
max_cores = 4
outdir = 'experiments/fairness_incast/'
envs = [('flexnic', False), ('linux', False)]
ncs = [50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 5000, 10000, 20000, 50000, 100000]
dirs = ['rx']
sz = 2048
totalmem = (1024+256)*1024*1024
totalbw=5000000


loop = asyncio.get_event_loop()

for i in range(0, tries):
  for di in dirs:
    for nc in ncs:
      for (env,ll) in envs:
        fn = '%s/%s_%d_%d_%s_%d.json' % (outdir, di, nc, int(ll), env, i)
        print('\n\n\n################################\n' + fn)
        if os.path.isfile(fn):
            print('Skipping %s because it already exists' % (fn))
            continue

        bufsz = 1 << (int(totalmem / nc / 2).bit_length() - 1)
        print('bufsz', bufsz)

        num_machines = int(min(nc, max_machines))
        num_cores = int(min(nc / num_machines, max_cores))
        conns_per_thread = int(nc / num_machines / num_cores)
        print(num_machines, num_cores, conns_per_thread)

        tx_del = 5
        if nc >= 100000:
            tx_del = 60

        params = {
                'msg_bytes': sz,
                'client_lowlevel': ll,
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
                'time': 60 + tx_del,
                'env': {
                    'env': env,
                    'fn_prog': 'fastemu',
                    'fn_threads': fn_threads,
                    'k_extra': [
                        '--tcp-rxbuf-len=' + str(bufsz),
                        '--tcp-txbuf-len=' + str(bufsz),
                        #'--cc-dctcp-weight=0.001',
                        #'--cc-dctcp-mimd=1.005',
                        #'--cc-dctcp-step=10000',
                        '--cc-dctcp-min=1000',
                        #'--cc=const-rate',
                        '--cc-const-rate=' + str(int(totalbw/nc)),
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
