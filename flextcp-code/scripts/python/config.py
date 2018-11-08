from infrastructure import HostConfig

animals_other = {
        'fn_cores': '3-7',
        'fn_memcs': '4',
        'flextcppath': '~/flextcp-code',
    }

CONFIG = {
        'hosts': [
            HostConfig('rhino', '10.0.0.10', '3c:fd:fe:9e:78:89', 'PASSWORD',
                animals_other),
            HostConfig('sloth', '10.0.0.11', '3c:fd:fe:9e:5d:61', 'PASSWORD',
                animals_other),
            ],
    }

try:
    import local_config
    for k,v in local_config.CONFIG.items():
        CONFIG[k] = v
except ImportError:
    pass
