"""Time-aligned YouTube playlist channels for WebV2 TV."""

import json
from pathlib import Path
from urllib.parse import parse_qs, urlparse


def _playlist_id(url):
    parsed = urlparse(url)
    if parsed.scheme != 'https' or parsed.hostname not in (
        'youtube.com', 'www.youtube.com', 'm.youtube.com'
    ):
        raise ValueError('playlist_url must be an HTTPS YouTube playlist URL.')
    playlist_id = parse_qs(parsed.query).get('list', [''])[0]
    if not playlist_id or len(playlist_id) > 128:
        raise ValueError('playlist_url must contain a YouTube list ID.')
    return playlist_id


class TVSchedule:
    def __init__(self, value):
        if not isinstance(value, dict):
            raise ValueError('Each TV channel must be an object.')
        self.channel_no = int(value['channel_no'])
        if self.channel_no < 1:
            raise ValueError('channel_no must be a positive integer.')
        self.name = str(value['name']).strip()
        if not self.name or len(self.name) > 80:
            raise ValueError('Channel name must contain 1 to 80 characters.')
        self.playlist_url = str(value['playlist_url']).strip()
        self.playlist_id = _playlist_id(self.playlist_url)
        self.start_minute = int(value.get('start_minute', 0)) % 1440
        self.program_minutes = int(value.get('program_minutes', 30))
        if not 1 <= self.program_minutes <= 1440:
            raise ValueError('program_minutes must be between 1 and 1440.')

    def position(self, hour, minute, seconds_per_game_minute=120):
        game_minute = (int(hour) % 24) * 60 + (int(minute) % 60)
        elapsed = (game_minute - self.start_minute) % 1440
        return {
            'channel_no': self.channel_no,
            'name': self.name,
            'playlist_id': self.playlist_id,
            'game_minute': game_minute,
            'program_index': elapsed // self.program_minutes,
            'start_seconds': (elapsed % self.program_minutes) * int(seconds_per_game_minute),
        }

    def public_data(self):
        return {
            'channel_no': self.channel_no,
            'name': self.name,
            'playlist_url': self.playlist_url,
            'start_minute': self.start_minute,
            'program_minutes': self.program_minutes,
        }


class TVPlaylist:
    def __init__(self, entries):
        if not isinstance(entries, list):
            raise ValueError('channels must be a JSON list.')
        self.channels = [TVSchedule(value) for value in entries]
        numbers = [channel.channel_no for channel in self.channels]
        if len(set(numbers)) != len(numbers):
            raise ValueError('TV channel numbers must be unique.')
        self.channels.sort(key=lambda channel: channel.channel_no)

    def public_data(self):
        return [channel.public_data() for channel in self.channels]


class TVChannelManager:
    def __init__(self, schedule_path=None):
        self.schedule_path = Path(schedule_path) if schedule_path else None
        self.seconds_per_game_minute = 120
        self.playlist = TVPlaylist([])
        if self.schedule_path is not None:
            self.load(self.schedule_path)

    def load(self, schedule_path=None):
        path = Path(schedule_path) if schedule_path else self.schedule_path
        if path is None:
            raise ValueError('TV schedule path is required.')
        raw = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(raw, dict):
            raise ValueError('TV schedule must be a JSON object.')
        seconds = int(raw.get('game_seconds_per_minute', 120))
        if not 1 <= seconds <= 3600:
            raise ValueError('game_seconds_per_minute must be between 1 and 3600.')
        playlist = TVPlaylist(raw.get('channels'))
        self.schedule_path = path
        self.seconds_per_game_minute = seconds
        self.playlist = playlist
        return playlist.channels

    def all_channels(self):
        return {
            'game_seconds_per_minute': self.seconds_per_game_minute,
            'channels': self.playlist.public_data(),
        }

    def position(self, channel_no, hour, minute):
        channel = next(
            (entry for entry in self.playlist.channels if entry.channel_no == int(channel_no)),
            None,
        )
        if channel is None:
            raise KeyError(channel_no)
        return channel.position(hour, minute, self.seconds_per_game_minute)
