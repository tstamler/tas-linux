import asyncio
import shlex

class HostConfig(object):
    def __init__(self, name, ip, mac, sudopwd, other={}):
        self.name = name
        self.ip = ip
        self.used_ip = ip
        self.mac = mac
        self.sudo_pwd = sudopwd
        self.other = other.copy()

class Component(object):
    def __init__(self, cmd_parts, with_stdin=False):
        self.is_ready = False
        self.stdout = []
        self.stdout_buf = bytearray()
        self.stderr = []
        self.stderr_buf = bytearray()
        self.cmd_parts = cmd_parts
        #print(cmd_parts)
        self.with_stdin = with_stdin

    def _parse_buf(self, buf, data):
        if data is not None:
            buf.extend(data)
        lines = []
        start = 0
        for i in range(0, len(buf)):
            if buf[i] == ord('\n'):
                l = buf[start:i].decode('utf-8')
                lines.append(l)
                start = i + 1
        del buf[0:start]

        if len(data) == 0 and len(buf) > 0:
            lines.append(buf.decode('utf-8'))
        return lines

    async def _consume_out(self, data):
        eof = len(data) == 0
        ls = self._parse_buf(self.stdout_buf, data)
        if len(ls) > 0 or eof:
            await self.process_out(ls, eof=eof)
            self.stdout = self.stdout + ls

    async def _consume_err(self, data):
        eof = len(data) == 0
        ls = self._parse_buf(self.stderr_buf, data)
        if len(ls) > 0 or eof:
            await self.process_err(ls, eof=eof)
            self.stderr = self.stderr + ls

    async def _read_stream(self, stream, fn):
        while True:
            bs = await stream.readline()
            if bs:
                await fn(bs)
            else:
                await fn(bs)
                return

    async def _waiter(self):
        out_handlers = asyncio.ensure_future(asyncio.wait([
            self._read_stream(self.proc.stdout, self._consume_out),
            self._read_stream(self.proc.stderr, self._consume_err)]))
        rc = await self.proc.wait()
        await out_handlers
        await self.terminated(rc)
        return rc

    async def send_input(self, bs, eof=False):
        self.proc.stdin.write(bs)
        if eof:
            self.proc.stdin.close()

    async def start(self):
        if self.with_stdin:
            stdin = asyncio.subprocess.PIPE
        else:
            stdin = None

        self.proc = await asyncio.create_subprocess_exec(*self.cmd_parts,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                stdin=stdin,
                )
        self.terminate_future = asyncio.ensure_future(self._waiter())
        await self.started()

    async def wait(self):
        await self.terminate_future
    async def terminate(self):
        self.proc.terminate()

    async def started(self):
        pass

    async def terminated(self, rc):
        pass

    async def process_out(self, lines, eof):
        pass

    async def process_err(self, lines, eof):
        pass

class RemoteComp(Component):
    def __init__(self, host, cmd_parts, cwd=None, **kwargs):
        if cwd is not None:
            cmd_parts = ['cd', cwd, '&&',
                    '(' + (' '.join(map(shlex.quote, cmd_parts))) + ')']
        parts = ['ssh', host.name, '--'] + cmd_parts
        #print(parts)
        super().__init__(parts, **kwargs)

class SudoCWDComp(RemoteComp):
    def __init__(self, host, cmd_parts, cwd=None, **kwargs):
        self.host = host
        shcmd = shlex.quote(self._list_to_shtok(cmd_parts))
        parts = ['sudo', '-S', '/bin/sh', '-c', shcmd]

        if cwd is not None:
            parts = ['cd', cwd, '&&'] + parts
        super().__init__(host, parts, with_stdin=True, **kwargs)

    def _list_to_shtok(self, l):
        return ' '.join(map(shlex.quote, l))

    async def started(self):
        await self.send_input((self.host.sudo_pwd + '\n').encode('utf-8'),
                eof=True)

class CPUSetComp(SudoCWDComp):
    def __init__(self, host, cpuset, cmd_parts, **kwargs):
        parts = ['cset', 'proc', '-s', cpuset, '-e', '--'] + cmd_parts
        super().__init__(host, parts, **kwargs)

class SimpleRemoteBase(object):
    def __init__(self, label, host, cmd_parts, verbose=False, canfail=False,
            *args, **kwargs):
        self.label = label
        self.host = host
        self.verbose = verbose
        self.canfail = canfail
        self.cmd_parts = cmd_parts
        super().__init__(host, cmd_parts, *args, **kwargs)

    async def process_out(self, lines, eof):
        if self.verbose:
            for l in lines:
                print(self.host.name, self.label, 'OUT:', lines)

    async def process_err(self, lines, eof):
        if self.verbose:
            for l in lines:
                print(self.host.name, self.label, 'ERR:', lines)

    async def terminated(self, rc):
        if self.verbose:
            print(self.host.name, self.label, 'TERMINATED:', rc)
        if not self.canfail and rc != 0:
            raise Exception('Command Failed: ' + str(self.cmd_parts))

class SimpleRemoteSudoCmd(SimpleRemoteBase, SudoCWDComp):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

class SimpleRemoteCmd(SimpleRemoteBase, RemoteComp):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
