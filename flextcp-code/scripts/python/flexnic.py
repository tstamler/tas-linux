import asyncio
import shlex
from infrastructure import (CPUSetComp, SudoCWDComp, SimpleRemoteSudoCmd,
        SimpleRemoteCmd)
from config import (CONFIG)

class FlexNICComp(CPUSetComp):
    def __init__(self, host, path, cores, mem_channels, threads, prog='flexnic',
            extra_args=[]):
        self.host = host
        self.ready_future = asyncio.Future()
        prog = 'flexnic/' + prog
        cmd = [prog, '-l', cores, '-n', mem_channels, '--'] + extra_args + \
                [str(threads)]
        super().__init__(host, 'user/flexnic', cmd, cwd=path)

    async def wait_ready(self):
        return await self.ready_future

    async def process_out(self, lines, eof):
        for l in lines:
            if l == 'flexnic ready':
                self.ready_future.set_result(True)
            print(self.host.name, 'flexnic OUT:', lines)

    async def terminated(self, rc):
        self.ready_future.cancel()

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'flexnic ERR:', lines)

class KernelComp(CPUSetComp):
    def __init__(self, host, path, ip, prefix_len=24, routes=[], extra_args=[]):
        self.host = host
        self.ready_future = asyncio.Future()
        cmd = ['kernel/kernel'] + extra_args + [ip + '/' + str(prefix_len)]
        super().__init__(host, 'user/kernel', cmd, cwd=path)

    async def wait_ready(self):
        return await self.ready_future

    async def process_out(self, lines, eof):
        for l in lines:
            if l == 'kernel ready':
                self.ready_future.set_result(True)
            print(self.host.name, 'kernel  OUT:', lines)

    async def terminated(self, rc):
        self.ready_future.cancel()

    async def process_err(self, lines, eof):
        for l in lines:
            print(self.host.name, 'kernel  ERR:', lines)

async def cleanup(host, path, additional=[]):
    procs = ['flexnic', 'kernel', 'fastemu'] + additional
    ka = SimpleRemoteSudoCmd('killall', host, ['killall']  + procs,
            canfail=True)
    await ka.start()
    await ka.wait()

    csd = SimpleRemoteSudoCmd('cpusets_destroy', host,
            ['scripts/cpusets_destroy.sh'], cwd=path)
    await csd.start()
    await csd.wait()

async def setup_dpdk(host, path):
    cs = SimpleRemoteSudoCmd('dpdk_init', host, ['scripts/dpdk_init.sh'],
            cwd=path, verbose=True)
    await cs.start()
    await cs.wait()

async def setup_linux(host, path, envs={}):
    parts = ['scripts/linux_stack.sh']
    if len(envs) > 0:
        eps = []
        for e in envs:
            eps.append(e + '=' + envs[e])
        parts = ['env'] + eps + parts
    cs = SimpleRemoteSudoCmd('linux_init', host, parts, cwd=path, verbose=True)
    await cs.start()
    await cs.wait()

async def setup_mtcp(host, path, params={}):
    myparams = {
            'num_cores': 1,
            'num_mem_ch': 2,
            'max_concurrency': 1024,
            'max_num_buffers': 1024,
            'rcvbuf': 16384,
            'sndbuf': 16384,
        }
    for p in params:
        myparams[p] = params[p]

    cs = SimpleRemoteSudoCmd('linux_mtcp', host, ['scripts/mtcp_init.sh'],
            cwd=path, verbose=True)
    cf = SimpleRemoteSudoCmd('mtcp_config', host, ['scripts/mtcp_config.sh',
            str(myparams['num_cores']), str(myparams['num_mem_ch']),
            str(myparams['max_concurrency']), str(myparams['max_num_buffers']),
            str(myparams['rcvbuf']), str(myparams['sndbuf'])], cwd=path, verbose=True)
    await cs.start()
    await cf.start()
    await cs.wait()
    await cf.wait()

async def setup_cpusets(host, path):
    cs = SimpleRemoteSudoCmd('cpusets', host, ['scripts/cpusets.sh'], cwd=path,
            verbose=True)
    await cs.start()
    await cs.wait()

async def setup_env(host, ip, additional, fn_threads,
        fn_prog, env, mtcp_params={}, fn_extra=[], k_extra=[]):

    await cleanup(host, host.other['flextcppath'], additional=additional)

    if env == 'flexnic':
        cs = setup_cpusets(host, host.other['flextcppath'])
        dp = setup_dpdk(host, host.other['flextcppath'])
        await cs
        await dp

        fn = FlexNICComp(host, host.other['flextcppath'],
            host.other['fn_cores'], host.other['fn_memcs'],
            fn_threads, fn_prog, fn_extra)
        print(fn_extra)

        k_extra = k_extra.copy()
        if 'routes' in host.other:
            for r in host.other['routes']:
                k_extra.append('--ip-route=' + r)
        k = KernelComp(host, host.other['flextcppath'], ip, extra_args=k_extra)
        print(k_extra)

        await fn.start()
        await fn.wait_ready()

        await k.start()
        await k.wait_ready()

        return [k, fn]

    elif env == 'linux':
        env = {
                'IP_OVERRIDE': ip + '/24',
            }
        if 'routes' in host.other:
            rs = ''
            for r in host.other['routes']:
                rs = rs + r + ' '
            env['ROUTES'] = rs

        dp = setup_linux(host, host.other['flextcppath'], env)
        await dp
        return []

    elif env == 'mtcp':
        dp = setup_mtcp(host, host.other['flextcppath'], mtcp_params)
        await dp
        return []

    elif env == 'dpdk':
        dp = setup_dpdk(host, host.other['flextcppath'])
        await dp
        return []

async def setup_envs(hosts, additional, fn_threads='1', fn_prog='flexnic',
        env='flexnic', mtcp_params={}, fn_extra=[], k_extra=[]):
    fns = []
    ks = []

    futs = []
    for h in hosts:
        futs.append(setup_env(h, h.used_ip, additional,
            fn_threads, fn_prog, env, mtcp_params, fn_extra, k_extra))
    futs,_ = await asyncio.wait(futs)

    procs = []
    for f in futs:
        ps = await f
        procs = procs + ps
    return procs

async def cleanup_envs(hosts, additional):
    futs = []
    for h in hosts:
        futs.append(cleanup(h, h.other['flextcppath'], additional=additional))
    await asyncio.wait(futs)

async def cleanup_all(additional=[]):
    a = ['echoserver_linux', 'testclient_linux', 'echoserver_mtcp',
            'testclient_mtcp'] + additional
    await cleanup_envs(CONFIG['hosts'], a)
