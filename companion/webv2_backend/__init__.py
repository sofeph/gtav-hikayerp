"""
Hikaye RP WebV2 Backend Engine
Flask/Twig benzeri şablon motoru ve eklenti API'sı.
"""

from .engine import WebV2Engine
from .template import TemplateEngine
from .plugin import PluginManager, PluginBase
from .tv import TVChannelManager, TVSchedule, TVPlaylist

__all__ = [
    'WebV2Engine',
    'TemplateEngine', 
    'PluginManager',
    'PluginBase',
    'TVChannelManager',
    'TVSchedule',
    'TVPlaylist',
]

__version__ = '2.0.0'