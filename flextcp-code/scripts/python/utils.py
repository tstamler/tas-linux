import asyncio
import sys
import importlib

def dropnone(l):
    xs = []
    for e in l:
        if e is not None:
            xs.append(e)
    return xs

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

def run(coro):
    loop = asyncio.get_event_loop().run_until_complete(coro)

def reloadall():
    for k,v in sys.modules.items():
        if not '__file__' in v.__dict__:
            continue
        if v.__file__.startswith('/usr'):
            continue
        print('reload', v)
        importlib.reload(v)
