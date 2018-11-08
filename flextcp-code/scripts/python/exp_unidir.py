import asyncio
from infrastructure import (CPUSetComp, SudoCWDComp, SimpleRemoteCmd,
        SimpleRemoteSudoCmd)
import flexnic
import re
import copy
import json
from config import (CONFIG)
from utils import *

class UnidirBase(object):
    def __init__(self, host, mode, threads, msg_bytes, max_flows, \
            tx_delay, op_delay, *args, **kwargs):
        self.ready_yet = False
        self.ready_future = asyncio.Future()
        self.host = host
        self.mode = mode
        self.threads = threads
        self.msg_bytes = msg_bytes
        self.max_flows = max_flows
        self.tx_delay = tx_delay
        self.op_delay = op_delay
        super().__init__(host, *args, **kwargs)

    async def wait_ready(self):
        return await self.ready_future

    async def process_out(self, lines, eof):
        for l in lines:
            if not self.ready_yet and l.startswith('unidir ready'):
                self.ready_yet = True
                self.ready_future.set_result(True)
            print(self.host.name, 'unidir OUT:', lines)

    async def terminated(self, rc):
        self.ready_future.cancel()

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'unidir ERR:', lines)

    def get_data(self):
        return {
                'host': self.host.name,
                'params': {
                    'mode': self.mode,
                    'threads': self.threads,
                    'msg_bytes': self.msg_bytes,
                    'max_flows': self.max_flows,
                    'tx_delay': self.tx_delay,
                },
                'stdout': self.stdout,
                'stderr': self.stderr,
            }

class UnidirFlexNICLLComp(UnidirBase, CPUSetComp):
    def __init__(self, host, path, ip=None, port=1234, mode='tx', threads=1, max_flows=32,
            msg_bytes=64, tx_delay=0, op_delay=0):
        cmd = ['mtcp/unidir_ll', '-c', str(max_flows), '-b', str(msg_bytes), \
            '-p', str(port), '-t', str(threads), '-d', str(tx_delay), \
            '-o', str(op_delay)]
        if mode != 'tx':
            cmd.append('-r')
        if ip is not None:
            cmd.append('-i')
            cmd.append(ip)
        super().__init__(host, mode, threads, msg_bytes, max_flows, tx_delay,
                op_delay, 'user/application', cmd, cwd=path)

class UnidirFlexNICComp(UnidirBase, CPUSetComp):
    def __init__(self, host, path, ip=None, port=1234, mode='tx', threads=1, max_flows=32,
            msg_bytes=64, tx_delay=0, op_delay=0):
        cmd = ['env', 'LD_PRELOAD=sockets/flextcp_interpose.so', \
            'mtcp/unidir_linux', '-c', str(max_flows), '-b', str(msg_bytes), \
            '-p', str(port), '-t', str(threads), '-d', str(tx_delay), \
            '-o', str(op_delay)]
        if mode != 'tx':
            cmd.append('-r')
        if ip is not None:
            cmd.append('-i')
            cmd.append(ip)
        super().__init__(host, mode, threads, msg_bytes, max_flows, tx_delay,
                op_delay, 'user/application', cmd, cwd=path)

class UnidirLinuxComp(UnidirBase, SudoCWDComp):
    def __init__(self, host, path, ip=None, port=1234, mode='tx', threads=1, max_flows=32,
            msg_bytes=64, tx_delay=0, op_delay=0):
        cmd = ['mtcp/unidir_linux', '-c', str(max_flows), '-b', str(msg_bytes), \
            '-p', str(port), '-t', str(threads), '-d', str(tx_delay), \
            '-o', str(op_delay)]
        if mode != 'tx':
            cmd.append('-r')
        if ip is not None:
            cmd.append('-i')
            cmd.append(ip)
        super().__init__(host, mode, threads, msg_bytes, max_flows, tx_delay,
                op_delay, cmd, cwd=path)

class UnidirMtcpComp(UnidirBase, SudoCWDComp):
    def __init__(self, host, path, ip=None, port=1234, mode='tx', threads=1, max_flows=32,
            msg_bytes=64, tx_delay=0, op_delay=0):
        cmd = ['mtcp/unidir_mtcp', '-c', str(max_flows), '-b', str(msg_bytes), \
            '-p', str(port), '-t', str(threads), '-d', str(tx_delay), \
            '-o', str(op_delay)]
        if mode != 'tx':
            cmd.append('-r')
        if ip is not None:
            cmd.append('-i')
            cmd.append(ip)
        super().__init__(host, mode, threads, msg_bytes, max_flows, tx_delay,
                op_delay, cmd, cwd=path)

class RouterComp(SudoCWDComp):
    def __init__(self, host, path, queues, queue_len=4096, queue_rate=10000,
            extra_args=[]):
        self.ready_yet = False
        self.ready_future = asyncio.Future()
        self.host = host
        self.queues = queues
        self.queue_len = queue_len
        self.queue_rate = queue_rate
        self.extra_args = extra_args

        queues = queues.copy()
        queue_params = []
        for q in queues:
            queue_params.append(q + ',' + str(queue_len) + ',' +
                    str(queue_rate))

        cmd = ['mtcp/router', '--'] + extra_args + queue_params
        print(cmd)
        super().__init__(host, cmd, cwd=path)

    async def wait_ready(self):
        return await self.ready_future

    async def process_out(self, lines, eof):
        for l in lines:
            if not self.ready_yet and l.startswith('router ready'):
                self.ready_yet = True
                self.ready_future.set_result(True)
            print(self.host.name, 'router OUT:', lines)

    async def terminated(self, rc):
        self.ready_future.cancel()

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'router ERR:', lines)

    def get_data(self):
        return {
                'host': self.host.name,
                'params': {
                    'queues': self.queues,
                    'queue_len': self.queue_len,
                    'queue_rate': self.queue_rate,
                    'extra_args': self.extra_args,
                },
                'stdout': self.stdout,
                'stderr': self.stderr,
            }


def make_data_dummy(params, server, services):
    return {
            'params': params,
            'server': server.get_data(),
            'services':
                [{'host': s.host.name, 'cmd_parts': s.cmd_parts, 'stdout': s.stdout,
                    'stderr': s.stderr} for s in services],
        }

def make_data_clients(params, server, clients, services):
    return {
            'params': params,
            'server': server.get_data(),
            'clients': [c.get_data() for c in clients],
            'services':
                [{'host': s.host.name, 'cmd_parts': s.cmd_parts, 'stdout': s.stdout,
                    'stderr': s.stderr} for s in services],
        }

def stack_to_class(stack, ll):
    if stack == 'flexnic':
        if ll:
            return UnidirFlexNICLLComp
        else:
            return UnidirFlexNICComp
    elif stack == 'mtcp':
        return UnidirMtcpComp
    else:
        return UnidirLinuxComp


# Params:
#  - time: seconds to run experiment
#  - server_params: parameters passed to server
#  - env: passed as args to flexnic.setup_envs
#       - env: linux | flexnic | mtcp
async def experiment_dummy(params):
    # when cleaning up make sure we kill all of those
    additional = ['unidir_ll', 'unidir_linux', 'unidir_mtcp', 'dummyem']

    # pick machines
    machines = CONFIG['hosts'][:1]
    server = machines[0]

    # setup environment on machines
    ps = await flexnic.setup_envs(machines, additional, env='flexnic', fn_prog='dummyem', **params['env'])


    server_params = params['server_params']

    # get components for echoserver and the clients
    if params['client_lowlevel']:
        es = UnidirFlexNICLLComp(server, server.other['flextcppath'], **server_params)
    else:
        es = UnidirFlexNICComp(server, server.other['flextcppath'], **server_params)

    # make sure flexnic and kernel are ready
    await asyncio.sleep(1)

    # Starting echo server
    await es.start()
    await es.wait_ready()

    # wait for spcified amount of time
    await asyncio.sleep(params['time'] + 1)

    # cleanup after ourselves
    await flexnic.cleanup_envs(machines, additional)
    for p in ps:
        await p.wait()
    await es.wait()

    return make_data_dummy(params, es, ps)

# Params:
#  - msg_bytes
#  - server_mode: (default tx)
#  - client_machines
#  - client_cores
#  - client_conns
#  - client_params
#  - client_lowlevel
#  - server_cores
#  - server_params: parameters passed to server
#  - client_params: parameters passed to server
#  - time: seconds to run experiment
#  - env: passed as args to flexnic.setup_envs
#       - env: linux | flexnic | mtcp
async def experiment(params):
    # when cleaning up make sure we kill all of those
    additional = ['unidir_linux', 'unidir_ll', 'unidir_mtcp', 'dummyem',
        'router']
    ps = []

    # pick machines
    machines = CONFIG['hosts'][:(1 + params['client_machines'])]
    all_machines = machines.copy()
    server = machines[0]
    server_ip = server.ip
    clients = machines[1:]

    # prepare env parameters
    env_s = params['env'].copy()
    env_cs = params['env'].copy()
    if 'env_server' in params:
        for p in params['env_server']:
            env_s[p] = copy.copy(params['env_server'][p])
    if 'env_clients' in params:
        for p in params['env_clients']:
            env_cs[p] = copy.copy(params['env_clients'][p])

    # prepare router
    has_router = 'drops' in params
    rc = None
    if has_router:
        router_mach = CONFIG['hosts'][-1]
        all_machines.append(router_mach)
        ps_r = await flexnic.setup_envs([router_mach], additional, env='dpdk')

        router_params = []
        if 'router_params' in params:
            router_params = params['router_params'].copy()

        if params['drops'] > 0.0:
            router_params += ['-d', str(params['drops'])]

        queues = []
        for mach in [server] + clients:
            mach_id = mach.ip.split('.')[3]
            mach_prefix = '10.0.' + mach_id + '.'
            mach_router = mach_prefix + '254'
            mach.used_ip = mach_prefix + '1'

            queues.append(mach.used_ip + ',' + mach.mac)
            mach.other['routes'] = ['10.0.0.0/16,' + mach_router]

        rc = RouterComp(router_mach, router_mach.other['flextcppath'], queues,
                extra_args=router_params)
        await rc.start()

        ps = ps + ps_r + [rc]

    # for mtcp env needs to contain the app core count
    if env_s['env'] == 'mtcp':
        if 'mtcp_params' not in env_s:
            env_s['mtcp_params'] = {}
        env_s['mtcp_params']['num_cores'] = params['server_cores']
    if env_cs['env'] == 'mtcp':
        if 'mtcp_params' not in env_cs:
            env_cs['mtcp_params'] = {}
        env_cs['mtcp_params']['num_cores'] = params['client_cores']

    # setup environment on machines
    pps_s = flexnic.setup_envs([server], additional, **env_s)
    pps_cs = flexnic.setup_envs(clients, additional, **env_cs)
    futs, _ = await asyncio.wait([pps_s, pps_cs])
    for f in futs:
        ps = ps + await f

    # figure out what classes to use for client and server components
    comp_class_s = stack_to_class(env_s['env'], params['client_lowlevel'])
    comp_class_cs = stack_to_class(env_cs['env'], params['client_lowlevel'])

    server_params = {}
    client_params = {}
    if 'server_params' in params:
        server_params = params['server_params']
    if 'client_params' in params:
        client_params = params['client_params']

    server_mode = 'tx'
    client_mode = 'rx'
    if 'server_mode' in params and params['server_mode'] == 'rx':
        server_mode = 'rx'
        client_mode = 'tx'

    # get components for echoserver and the clients
    ccs = []
    port = 1234
    msg_bytes = params['msg_bytes']
    server_max_flows = int(params['client_machines'] * params['client_cores'] * \
            params['client_conns'])
    es = comp_class_s(server, server.other['flextcppath'], port=port, mode=server_mode,
            msg_bytes=msg_bytes, max_flows=server_max_flows,
            threads=params['server_cores'], **server_params)
    for cm in clients:
        ccs.append(comp_class_cs(cm, cm.other['flextcppath'], port=port,
            ip=server.used_ip, mode=client_mode, msg_bytes=msg_bytes,
            max_flows=params['client_conns'], threads=params['client_cores'],
            **client_params))

    # if router used, wait till it's ready
    if rc:
        await rc.wait_ready()

    # make sure flexnic and kernel are ready
    await asyncio.sleep(1)

    # Starting server server
    await es.start()
    await es.wait_ready()

    # Make sure everything is ready
    await asyncio.sleep(1)

    # start clients
    k = 0
    for c in ccs:
        await c.start()
        await asyncio.sleep(0.5)

    # wait for spcified amount of time
    await asyncio.sleep(params['time'])

    # cleanup after ourselves
    await flexnic.cleanup_envs(all_machines, additional)
    for p in ps:
        await p.wait()
    await es.wait()
    for c in ccs:
        await c.wait()

    return make_data_clients(params, es, ccs, ps)
