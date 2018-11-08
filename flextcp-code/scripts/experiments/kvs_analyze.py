import re
import glob
import sys
import os

basedir_name = sys.argv[1]

def parse_experiment(scn):
    m = re.match(r'conns_stack([^_]+)_fc(\d+)_sc(\d+)_cc(\d+)', scn)
    if not m:
        return None

    obj = {}
    obj['params'] = {
        'stack': m.group(1),
        'flexnic_cores': int(m.group(2)),
        'server_cores': int(m.group(3)),
        'client_conns': int(m.group(4))
        }
    obj['clients'] = {}

    dir_path = basedir_name + '/' + scn
    for log in os.listdir(dir_path):
        m = re.match(r'client_([^\.]+).log', log)
        if m is None:
            continue
        client_name = m.group(1)
        with open(dir_path + '/' + log, 'r') as f:
            client = parse_client(f)
        obj['clients'][client_name] = client
    return obj

def parse_client(f):
    client = {'measurements': []}
    meas = None
    for l in f:
        l = l[:-1]

        conn_m = re.match(r'O ([^:]+):\s+t\[(\d+)\]\.conns\[(\d+)\]: +pend=(\d+) +cnt=(\d+)', l)
        if conn_m is not None:
            cid = (int(conn_m.group(2)), int(conn_m.group(3)))
            conn = {
                'pend': int(conn_m.group(4)),
                'cnt': int(conn_m.group(5))
                }
            assert cid not in meas['conns']
            meas['conns'][cid] = conn
            continue

        tp_m = re.match(r'O ([^:]+): TP: total=([0-9\.]+) mops  50p=(-?[0-9]+) us  90p=(-?[0-9]+) us  95p=(-?[0-9]+) us  99p=(-?[0-9]+) us  99\.9p=(-?[0-9]+) us  99.99p=(-?[0-9]+) us.*', l)
        if tp_m is not None:
            meas = {
                'tput': float(tp_m.group(2)),
                '50p': int(tp_m.group(3)),
                '90p': int(tp_m.group(4)),
                '95p': int(tp_m.group(5)),
                '99p': int(tp_m.group(6)),
                '99.9p': int(tp_m.group(7)),
                '99.99p': int(tp_m.group(8)),
                'conns': {},
                'cores': {}
                }
            client['measurements'].append(meas)
            continue

    return client

def klist(l, k):
    xs = []
    for e in l:
        xs.append(e[k])
    return xs

def list_stats(l):
    if len(l) == 0:
        return {'min': None, 'max': None, 'mean': None, 'num': 0}
    xs = sorted(l)
    return {
        'min': xs[0],
        'max': xs[-1],
        'mean': float(sum(xs)) / len(xs),
        'num': len(xs)
        }


invalid_params = []
lines = []
exs = []
for scn in os.listdir(basedir_name):
    ex = parse_experiment(scn)
    print scn
    tput_total = 0.0
    p50_total = []
    p90_total = []
    p99_total = []
    p999_total = []
    valid = True
    for cn in ex['clients'].keys():
        print '  ' + cn + ':'
        c = ex['clients'][cn]

        measurements = c['measurements']
        if len(measurements) < 30:
            valid = False
        else:
            measurements[10:][:10]
        p50_l = klist(measurements, '50p')
        p90_l = klist(measurements, '90p')
        p99_l = klist(measurements, '99p')
        p999_l = klist(measurements, '99.9p')

        tp_stats = list_stats(klist(c['measurements'], 'tput'))
        p50_stats = list_stats(p50_l)
        p90_stats = list_stats(p90_l)
        p99_stats = list_stats(p99_l)
        p999_stats = list_stats(p999_l)

        if tp_stats['mean'] is not None:
            tput_total += tp_stats['mean']
        p50_total += p50_l
        p90_total += p90_l
        p99_total += p99_l
        p999_total += p999_l

        print '     tput:', tp_stats
        print '    50.0p:', p50_stats
        print '    90.0p:', p90_stats
        print '    99.0p:', p99_stats
        print '    99.9p:', p999_stats

    print '  total:'
    print '     tput:', tput_total, 'mops'
    print '    50.0p:', list_stats(p50_total)
    print '    90.0p:', list_stats(p90_total)
    print '    99.0p:', list_stats(p99_total)
    print '    99.9p:', list_stats(p999_total)

    if valid:
        lines.append('\t'.join(map(str, [
            ex['params']['stack'],
            ex['params']['flexnic_cores'],
            ex['params']['server_cores'],
            ex['params']['client_conns'],
            tput_total,
            list_stats(p50_total)['mean'],
            list_stats(p90_total)['mean'],
            list_stats(p99_total)['mean'],
            list_stats(p999_total)['mean'],
            list_stats(p50_total)['num']
            ])))
        exs.append(ex)
    else:
        invalid_params.append(ex['params'])

print '\n\n\nRaw Data:'
print '\n'.join(lines)

print '\n\n\nInvalid runs:'
print '\n'.join(map(str, invalid_params))

print '\n\n\nRelevant numbers:'

fc=4
cc=4
tp_lines = []
lat_lines = []
for sc in [1, 2, 4]:
    print 'Cores', sc
    tputs = [sc]
    percs = [sc]
    for st in ['linux', 'mtcp', 'flextcp']:
        print '   ', st
        # find experiment
        ex = None
        for e in exs:
            p = e['params']
            if p['flexnic_cores'] == fc and p['server_cores'] == sc and \
                    p['client_conns'] == cc and p['stack'] == st:
                ex = e
                break

        tput = 0.0
        p50_total = []
        p90_total = []
        p99_total = []
        p999_total = []

        for cn in ex['clients'].keys():
            c = ex['clients'][cn]
            measurements = c['measurements'][10:][:10]
            tp_stats = list_stats(klist(measurements, 'tput'))
            tput += tp_stats['mean']
            p50_total += klist(measurements, '50p')
            p90_total += klist(measurements, '90p')
            p99_total += klist(measurements, '99p')
            p999_total += klist(measurements, '99.9p')

            print '       ', tp_stats

        tputs.append(tput)
        percs.append(list_stats(p50_total)['mean'])
        percs.append(list_stats(p90_total)['mean'])
        percs.append(list_stats(p99_total)['mean'])
        percs.append(list_stats(p999_total)['mean'])
        percs.append('')
    tp_lines.append('\t'.join(map(str, tputs)))
    lat_lines.append('\t'.join(map(str, percs)))
print '\n'.join(tp_lines)
print
print '\n'.join(lat_lines)
