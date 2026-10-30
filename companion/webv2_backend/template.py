"""Jinja/Twig-style HTML template rendering for WebV2 plugins."""

from pathlib import Path
from jinja2 import Environment, FileSystemLoader, ChoiceLoader, select_autoescape


class TemplateEngine:
    def __init__(self, template_root):
        self.template_root = Path(template_root).resolve()
        self.template_root.mkdir(parents=True, exist_ok=True)
        self._paths = [self.template_root]
        self._environment = self._make_environment()

    def _make_environment(self):
        loader = FileSystemLoader([str(path) for path in self._paths])
        return Environment(loader=loader, autoescape=select_autoescape(('html', 'htm', 'xml')))

    def add_path(self, path):
        resolved = Path(path).resolve()
        if resolved not in self._paths:
            self._paths.append(resolved)
            self._environment = self._make_environment()

    def render(self, template_name, context=None):
        if not isinstance(template_name, str) or not template_name or Path(template_name).is_absolute() or '..' in Path(template_name).parts:
            raise ValueError('Template name must be a safe relative path.')
        template = self._environment.get_template(template_name)
        return template.render(**(context or {}))