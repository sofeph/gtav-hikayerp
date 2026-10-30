"""Plugin discovery and registration for local, explicitly installed WebV2 plugins."""

import importlib.util
import re
import sys
from pathlib import Path


class PluginBase:
    name = None
    version = '0.1.0'

    def register(self, app):
        raise NotImplementedError


class PluginManager:
    def __init__(self, app):
        self.app = app
        self.loaded = {}

    def load_plugins(self, plugin_dir):
        root = Path(plugin_dir).resolve()
        if not root.exists():
            return []
        if not root.is_dir():
            raise NotADirectoryError(str(root))

        loaded_now = []
        for entry in sorted(root.iterdir(), key=lambda path: path.name.casefold()):
            if entry.name.startswith('_'):
                continue
            if entry.is_dir():
                manifest = entry / 'plugin.py'
                if not manifest.is_file():
                    continue
                module_path = manifest
                plugin_name = entry.name
                template_path = entry / 'templates'
            elif entry.is_file() and entry.suffix == '.py' and entry.name != '__init__.py':
                module_path = entry
                plugin_name = entry.stem
                template_path = root / plugin_name / 'templates'
            else:
                continue

            if not re.fullmatch(r'[A-Za-z0-9_-]{1,48}', plugin_name):
                raise ValueError(f'Invalid WebV2 plugin name: {plugin_name}')
            if plugin_name in self.loaded:
                continue
            module_key = f'_hikayerp_webv2_plugin_{plugin_name}'
            spec = importlib.util.spec_from_file_location(module_key, module_path)
            if spec is None or spec.loader is None:
                raise ImportError(f'Cannot load WebV2 plugin: {module_path}')
            module = importlib.util.module_from_spec(spec)
            sys.modules[module_key] = module
            try:
                spec.loader.exec_module(module)
                plugin = getattr(module, 'plugin', None)
                if plugin is not None:
                    if not isinstance(plugin, PluginBase):
                        raise TypeError(f'{plugin_name}: plugin must extend PluginBase')
                    plugin.register(self.app)
                elif callable(getattr(module, 'register', None)):
                    module.register(self.app)
                else:
                    raise TypeError(f'{plugin_name}: define register(app) or plugin = PluginBase()')
            except Exception:
                sys.modules.pop(module_key, None)
                raise

            if template_path.is_dir():
                self.app.template_engine.add_path(template_path)
            self.loaded[plugin_name] = module
            loaded_now.append(plugin_name)
        return loaded_now