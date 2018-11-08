import asyncio
from infrastructure import (CPUSetComp, SudoCWDComp, SimpleRemoteCmd,
        SimpleRemoteSudoCmd)
import flexnic
import re
from config import (CONFIG)
from utils import *

def make_data(params, server, clients, services):
    return {
            'params': params,
            'server': server.get_data(),
            'clients': [c.get_data() for c in clients],
            'services':
                [{'host': s.host.name, 'cmd_parts': s.cmd_parts, 'stdout': s.stdout,
                    'stderr': s.stderr} for s in services],
        }

class FlexKVSBase(object):
    def __init__(self, host, cmd_base, threads=1, **kwargs):
        self.ready_future = asyncio.Future()
        self.host = host
        self.threads = threads
        super().__init__(host, cmd_parts=cmd_base + self.get_args(), **kwargs)

    def get_args(self):
        return ['/tmp/mtcp.conf', str(self.threads)]


    async def wait_ready(self):
        return await self.ready_future

    async def process_out(self, lines, eof):
        for l in lines:
            if l == 'Starting maintenance':
                self.ready_future.set_result(True)
            print(self.host.name, 'flexkvs OUT:', lines)

    async def terminated(self, rc):
        self.ready_future.cancel()

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'flexkvs ERR:', lines)

    def get_data(self):
        return {
                'host': self.host.name,
                'params': {
                    'threads': self.threads,
                },
                'stdout': self.stdout,
                'stderr': self.stderr,
            }

class KVSBenchBase(object):
    def __init__(self, host, cmd_base, server_ip, threads=1, conns=1, warmup=10,
            cooldown=10, time=30, extra=[], **kwargs):
        self.host = host
        self.threads = threads
        self.server_ip = server_ip
        self.conns = conns
        self.warmup = warmup
        self.cooldown = cooldown
        self.time = time
        self.extra = extra
        super().__init__(host, cmd_parts=cmd_base + self.get_args(), **kwargs)

    def get_args(self):
        return ['-t', str(self.threads), '-C', str(self.conns),
                '-w', str(self.warmup), '-c', str(self.cooldown),
                '-T', str(self.time)] + self.extra + \
                [self.server_ip + ':11211']

    async def process_out(self, lines, eof):
        for l in lines:
            print(self.host.name, 'kvsbench OUT:', lines)

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'kvsbench ERR:', lines)

    def get_data(self):
        return {
                'host': self.host.name,
                'params': {
                    'threads': self.threads,
                    'conns': self.conns,
                    'warmup': self.warmup,
                    'cooldown': self.cooldown,
                    'time': self.time,
                    'extra': self.extra,
                },
                'stdout': self.stdout,
                'stderr': self.stderr,
            }



class FlexKVSFlexNICComp(FlexKVSBase, CPUSetComp):
    def __init__(self, host, path, **kwargs):
        cmd = ['env', 'LD_PRELOAD=sockets/flextcp_interpose.so',
                'flexkvs/flexkvs']
        super().__init__(host, cmd, cpuset='user/application', cwd=path, **kwargs)

class FlexKVSLinuxComp(FlexKVSBase, SudoCWDComp):
    def __init__(self, host, path, **kwargs):
        cmd = ['flexkvs/flexkvs']
        super().__init__(host, cmd, cwd=path, **kwargs)

class FlexKVSMtcpComp(FlexKVSBase, SudoCWDComp):
    def __init__(self, host, path, **kwargs):
        self.path = path
        cmd = ['flexkvs/flexkvs-mtcp']
        super().__init__(host, cmd, cwd=path, **kwargs)

    async def start(self):
        lc = SimpleRemoteSudoCmd('limitcores', self.host,
            ['scripts/limitcores.sh', '16'], cwd=self.path)
        await lc.start()
        await lc.wait()
        return await super().start()


class KVSBenchFlexNICComp(KVSBenchBase, CPUSetComp):
    def __init__(self, host, path, **kwargs):
        cmd = ['env', 'LD_PRELOAD=sockets/flextcp_interpose.so',
                'flexkvs/kvsbench']
        super().__init__(host, cmd, cpuset='user/application', cwd=path, **kwargs)

class KVSBenchLinuxComp(KVSBenchBase, SudoCWDComp):
    def __init__(self, host, path, **kwargs):
        cmd = ['flexkvs/kvsbench']
        super().__init__(host, cmd, cwd=path, **kwargs)

class KVSBenchMtcpComp(KVSBenchBase, SudoCWDComp):
    def __init__(self, host, path, **kwargs):
        self.path = path
        cmd = ['flexkvs/kvsbench-mtcp']
        super().__init__(host, cmd, cwd=path, **kwargs)

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
    additional = ['flexkvs', 'flexkvs-mtcp', 'kvsbench', 'kvsbench-mtcp']
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
        server_comp_class = FlexKVSFlexNICComp
        client_comp_class = KVSBenchFlexNICComp
    elif stack == 'mtcp':
        server_comp_class = FlexKVSMtcpComp
        client_comp_class = KVSBenchMtcpComp
    else:
        server_comp_class = FlexKVSLinuxComp
        client_comp_class = KVSBenchLinuxComp

    server_params = {}
    client_params = {}
    if 'server_params' in params:
        server_params = params['server_params']
    if 'client_params' in params:
        client_params = params['client_params']

    # get components for echoserver and the clients
    ccs = []
    es = server_comp_class(server, server.other['flextcppath'],
            threads=params['server_cores'], **server_params)
    for cm in clients:
        ccs.append(client_comp_class(cm, server.other['flextcppath'],
            server_ip=server.ip, conns=params['client_conns'],
                threads=params['client_cores'], **client_params))

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
