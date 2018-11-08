import asyncio
from infrastructure import (CPUSetComp, SudoCWDComp, SimpleRemoteCmd,
        SimpleRemoteSudoCmd)
import flexnic
import re
from config import (CONFIG)
from utils import *

class RPCuBenchResult(object):
    def __init__(self, data):
        self.data = data

    def print_stats(self):
        for c in self.data['clients']:
            print('  ' + c['host'])

            out = self.parse_client(c)
            tp_s = list_stats(klist(out, 'tput'))
            to_s = list_stats(klist(out, 'tput_ops'))
            p50_s = list_stats(klist(out, '50p'))
            p99_s = list_stats(klist(out, '99p'))
            p9999_s = list_stats(dropnone(klist(out, '99.99p')))
            print('      mBPS: ', tp_s['mean'], '\tmin=', tp_s['min'], '\tmax=', tp_s['max'])
            print('     mOPPS: ', to_s['mean'], '\tmin=', to_s['min'], '\tmax=', to_s['max'])
            print('       50p: ', p50_s['mean'], '\tmin=', p50_s['min'], '\tmax=', p50_s['max'])
            print('       99p: ', p99_s['mean'], '\tmin=', p99_s['min'], '\tmax=', p99_s['max'])
            print('       99p: ', p9999_s['mean'], '\tmin=', p9999_s['min'], '\tmax=', p9999_s['max'])

    def get_tput(self, drop_first=0, drop_last=0):
        tput = 0.0
        for c in self.data['clients']:
            out = self.parse_client(c)
            tputs = klist(out, 'tput')
            if drop_first > 0:
                tputs = tputs[drop_first:]
            if drop_last > 0:
                tputs = tputs[:-drop_last]
            nt = 0.0
            if len(tputs) > 0:
                nt = sum(tputs)/len(tputs)
            tput = tput + nt
        return tput

    def parse_int(self, s):
        return int(s.replace(',', ''))

    def parse_float(self, s):
        return float(s.replace(',', ''))

    def negnone(self, x):
        i = self.parse_int(x)
        if i == -1:
            return None
        return i

    def parse_client(self, c):
        meass = []
        meas = None
        msg_bytes = c['params']['msg_bytes']
        for l in c['stdout']:
            conn_m = re.match(r'\s+t\[(\d+)\]\.conns\[(\d+)\]: +pend=(\d+) +rx_r=\d+ +tx_r=\d+ +cnt=(\d+) +fd=\d+', l)
            if conn_m is not None:
                cid = (self.parse_int(conn_m.group(1)), self.parse_int(conn_m.group(2)))
                conn = {
                    'pend': self.parse_int(conn_m.group(3)),
                    'cnt': self.parse_int(conn_m.group(4))
                    }
                assert cid not in meas['conns']
                meas['conns'][cid] = conn
                continue

            tp_m = re.match(r'TP: total=([0-9\.,]+) mbps  50p=(-?[0-9,]+) us  90p=(-?[0-9,]+) us  95p=(-?[0-9,]+) us  99p=(-?[0-9,]+) us  99\.9p=(-?[0-9,]+) us  99.99p=(-?[0-9,]+) us.*', l)
            if tp_m is not None:
                meas = {
                    'tput': self.parse_float(tp_m.group(1)),
                    'tput_ops': self.parse_float(tp_m.group(1)) / 8. / float(msg_bytes),
                    '50p': self.negnone(tp_m.group(2)),
                    '90p': self.negnone(tp_m.group(3)),
                    '95p': self.negnone(tp_m.group(4)),
                    '99p': self.negnone(tp_m.group(5)),
                    '99.9p': self.negnone(tp_m.group(6)),
                    '99.99p': self.negnone(tp_m.group(7)),
                    'conns': {},
                    'cores': {}
                    }
                meass.append(meas)
                continue
        return meass

def make_data(params, server, clients, services):
    return {
            'params': params,
            'server': server.get_data(),
            'clients': [c.get_data() for c in clients],
            'services':
                [{'host': s.host.name, 'cmd_parts': s.cmd_parts, 'stdout': s.stdout,
                    'stderr': s.stderr} for s in services],
        }

class EchoserverBase(object):
    def __init__(self, host, threads, max_flows, msg_bytes, *args, **kwargs):
        self.ready_future = asyncio.Future()
        self.host = host
        self.threads = threads
        self.max_flows = max_flows
        self.msg_bytes = msg_bytes
        super().__init__(host, *args, **kwargs)

    async def wait_ready(self):
        return await self.ready_future

    async def process_out(self, lines, eof):
        for l in lines:
            if l == 'Workers ready':
                self.ready_future.set_result(True)
            print(self.host.name, 'echosrv OUT:', lines)

    async def terminated(self, rc):
        self.ready_future.cancel()

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'echosrv ERR:', lines)

    def get_data(self):
        return {
                'host': self.host.name,
                'params': {
                    'threads': self.threads,
                    'max_flows': self.max_flows,
                    'msg_bytes': self.msg_bytes,
                },
                'stdout': self.stdout,
                'stderr': self.stderr,
            }

class RPCuBenchBase(object):
    def __init__(self, host, threads, msg_bytes, max_pending, num_conns, *args,
            **kwargs):
        self.host = host
        self.threads = threads
        self.msg_bytes = int(msg_bytes)
        self.max_pending = max_pending
        self.num_conns = num_conns
        super().__init__(host, *args, **kwargs)

    async def process_out(self, lines, eof):
        for l in lines:
            print(self.host.name, 'rpcbenc OUT:', lines)

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'rpcbenc ERR:', lines)

    def get_data(self):
        return {
                'host': self.host.name,
                'params': {
                    'threads': self.threads,
                    'msg_bytes': self.msg_bytes,
                    'max_pending': self.max_pending,
                    'num_conns': self.num_conns,
                },
                'stdout': self.stdout,
                'stderr': self.stderr,
            }



class EchoserverFlexNICComp(EchoserverBase, CPUSetComp):
    def __init__(self, host, path, port=1234, threads=1, max_flows=32,
            msg_bytes=64):
        config = 'dummy'
        cmd = ['env', 'LD_PRELOAD=sockets/flextcp_interpose.so',
                'mtcp/echoserver_linux', str(port), str(threads), config,
                str(max_flows), str(msg_bytes)]
        super().__init__(host, threads, max_flows, msg_bytes,
                'user/application', cmd, cwd=path)

class EchoserverLinuxComp(EchoserverBase, SudoCWDComp):
    def __init__(self, host, path, port=1234, threads=1, max_flows=32,
            msg_bytes=64):
        config = 'dummy'
        cmd = ['mtcp/echoserver_linux', str(port), str(threads), config,
                str(max_flows), str(msg_bytes)]
        super().__init__(host, threads, max_flows, msg_bytes, cmd, cwd=path)

class EchoserverMtcpComp(EchoserverBase, SudoCWDComp):
    def __init__(self, host, path, port=1234, threads=1,
            config='mtcp/echoserver.conf', max_flows=32, msg_bytes=64):
        self.threads = threads
        self.in_config = config
        self.path = path
        self.use_config = '/tmp/mtcp.conf'
        cmd = ['env', 'MTCP_LOGDIR=/tmp', 'mtcp/echoserver_mtcp', str(port),
                str(threads), self.use_config, str(max_flows), str(msg_bytes)]
        super().__init__(host, threads, max_flows, msg_bytes, cmd, cwd=path)

    async def start(self):
        lc = SimpleRemoteSudoCmd('limitcores', self.host,
            ['scripts/limitcores.sh', '16'], cwd=self.path)
        await lc.start()
        await lc.wait()
        return await super().start()


class RPCuBenchFlexNICComp(RPCuBenchBase, CPUSetComp):
    def __init__(self, host, path, ip, port=1234, threads=8,
            config='mtcp/testclient.conf', msg_bytes=64, max_pending=1,
            num_conns=1):
        cmd = ['env', 'LD_PRELOAD=sockets/flextcp_interpose.so',
                'mtcp/testclient_linux', ip, str(port), str(threads), config,
                str(msg_bytes), str(max_pending), str(num_conns)]
        super().__init__(host, threads, msg_bytes, max_pending, num_conns,
                'user/application', cmd, cwd=path)

class RPCuBenchLinuxComp(RPCuBenchBase, SudoCWDComp):
    def __init__(self, host, path, ip, port=1234, threads=8,
            config='mtcp/testclient.conf', msg_bytes=64, max_pending=1,
            num_conns=1):
        cmd = ['mtcp/testclient_linux', ip, str(port), str(threads), config,
                str(msg_bytes), str(max_pending), str(num_conns)]
        super().__init__(host, threads, msg_bytes, max_pending, num_conns, cmd,
                cwd=path)

class RPCuBenchMtcpComp(RPCuBenchBase, SudoCWDComp):
    def __init__(self, host, path, ip, port=1234, threads=8,
            config='mtcp/testclient.conf', msg_bytes=64, max_pending=1,
            num_conns=1):
        self.path = path
        self.threads = threads
        self.in_config = config
        self.use_config = '/tmp/mtcp.conf'
        cmd = ['env', 'MTCP_LOGDIR=/tmp', 'mtcp/testclient_mtcp', ip, str(port),
                str(threads), self.use_config, str(msg_bytes), str(max_pending),
                str(num_conns)]
        super().__init__(host, threads, msg_bytes, max_pending, num_conns, cmd,
                cwd=path)

    async def start(self):
        lc = SimpleRemoteSudoCmd('limitcores', self.host,
            ['scripts/limitcores.sh', '16'], cwd=self.path)
        await lc.start()
        await lc.wait()
        return await super().start()

# Params:
#  - client_machines
#  - client_cores
#  - client_conns
#  - server_cores
#  - time: seconds to run experiment
#  - env: passed as args to flexnic.setup_envs
#       - env: linux | flexnic | mtcp
async def experiment(params):
    # when cleaning up make sure we kill all of those
    additional = ['echoserver_linux', 'echoserver_mtcp', 'testclient_linux',
        'testclient_mtcp']
    ps = []

    # pick machines
    machines = CONFIG['hosts'][:(1 + params['client_machines'])]
    server = machines[0]
    clients = machines[1:]

    server_max_flows = params['client_machines'] * params['client_cores'] * \
            params['client_conns'] * 2
    client_max_flows = params['client_cores'] * params['client_conns']

    # prepare env parameters
    env_s = params['env'].copy()
    env_cs = params['env'].copy()
    if 'env_server' in params:
        for p in params['env_server']:
            env_s[p] = copy.copy(params['env_server'][p])
    if 'env_clients' in params:
        for p in params['env_clients']:
            env_cs[p] = copy.copy(params['env_clients'][p])

    # for mtcp env needs to contain the app core count
    if env_s['env'] == 'mtcp':
        if 'mtcp_params' not in env_s:
            env_s['mtcp_params'] = {}
        env_s['mtcp_params']['num_cores'] = params['server_cores']
        if server_max_flows > 1024:
            env_s['mtcp_params']['max_concurrency'] = server_max_flows
            env_s['mtcp_params']['max_num_buffers'] = server_max_flows
    if env_cs['env'] == 'mtcp':
        if 'mtcp_params' not in env_cs:
            env_cs['mtcp_params'] = {}
        env_cs['mtcp_params']['num_cores'] = params['client_cores']
        if client_max_flows > 1024:
            env_cs['mtcp_params']['max_concurrency'] = client_max_flows
            env_cs['mtcp_params']['max_num_buffers'] = client_max_flows

    # setup environment on machines
    pps_s = flexnic.setup_envs([server], additional, **env_s)
    pps_cs = flexnic.setup_envs(clients, additional, **env_cs)
    futs, _ = await asyncio.wait([pps_s, pps_cs])
    for f in futs:
        ps = ps + await f

    # figure out what classes to use for client and server components
    stack = params['env']['env']
    if stack == 'flexnic':
        server_comp_class = EchoserverFlexNICComp
        client_comp_class = RPCuBenchFlexNICComp
    elif stack == 'mtcp':
        server_comp_class = EchoserverMtcpComp
        client_comp_class = RPCuBenchMtcpComp
    else:
        server_comp_class = EchoserverLinuxComp
        client_comp_class = RPCuBenchLinuxComp

    server_params = {}
    client_params = {}
    if 'server_params' in params:
        server_params = params['server_params']
    if 'client_params' in params:
        client_params = params['client_params']

    # get components for echoserver and the clients
    ccs = []
    es = server_comp_class(server, server.other['flextcppath'],
            max_flows=server_max_flows, threads=params['server_cores'],
            **server_params)
    for cm in clients:
        ccs.append(client_comp_class(cm, server.other['flextcppath'],
            server.ip, num_conns=params['client_conns'],
                threads=params['client_cores'],
                **client_params))

    # make sure flexnic and kernel are ready
    await asyncio.sleep(1)

    # Starting echo server
    await es.start()
    await es.wait_ready()

    # Make sure everything is ready
    await asyncio.sleep(1)

    # start clients
    for c in ccs:
        await c.start()
        await asyncio.sleep(0.5)

    # wait for spcified amount of time
    await asyncio.sleep(params['time'])

    # cleanup after ourselves
    await flexnic.cleanup_envs(machines, additional)
    for p in ps:
        await p.wait()
    await es.wait()
    for c in ccs:
        await c.wait()

    return make_data(params, es, ccs, ps)
