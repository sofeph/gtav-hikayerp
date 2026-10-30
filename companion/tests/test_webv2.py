import json
from pathlib import Path

import pytest

from webv2_backend.engine import Request, Response, WebV2Engine
from webv2_backend.tv import TVChannelManager


ROOT = Path(__file__).resolve().parents[2]


def test_tv_channel_json_uses_playlist_url_and_channel_number():
    manager = TVChannelManager(ROOT / 'data' / 'webv2' / 'tv' / 'channels.json')

    channels = manager.all_channels()

    assert channels['game_seconds_per_minute'] == 120
    assert [channel['channel_no'] for channel in channels['channels']] == [1, 2]
    assert channels['channels'][0]['name'] == 'Weazel TV'
    assert channels['channels'][0]['playlist_url'].startswith('https://www.youtube.com/playlist?list=')


def test_tv_schedule_computes_playlist_slot_from_game_time(tmp_path):
    schedule = tmp_path / 'channels.json'
    schedule.write_text(json.dumps({
        'game_seconds_per_minute': 120,
        'channels': [{
            'channel_no': 7,
            'name': 'Test Channel',
            'playlist_url': 'https://www.youtube.com/playlist?list=PL_test',
            'start_minute': 30,
            'program_minutes': 10,
        }],
    }), encoding='utf-8')
    manager = TVChannelManager(schedule)

    position = manager.position(7, 1, 5)

    assert position['program_index'] == 3
    assert position['start_seconds'] == 600


def test_engine_routes_converts_path_parameters_and_keeps_session():
    engine = WebV2Engine(data_root=ROOT / 'data')

    @engine.get('/items/<int:item_id>')
    def item(request, item_id):
        request.session['item'] = item_id
        return {'item': item_id, 'session': request.session['item']}

    response = engine.handle_request(Request(path='/items/42'))

    assert response.status == 200
    assert json.loads(response.body) == {'item': 42, 'session': 42}
    assert response.cookies['webv2_session']['value']


def test_engine_returns_not_found_without_a_template_dependency():
    engine = WebV2Engine(data_root=ROOT / 'data')

    response = engine.handle_request(Request(path='/missing'))

    assert response.status == 404
    assert response.body == 'Not found.'


def test_engine_rejects_duplicate_route():
    engine = WebV2Engine(data_root=ROOT / 'data')
    engine.get('/same')(lambda request: Response().text('first'))

    with pytest.raises(ValueError, match='Duplicate WebV2 route'):
        engine.get('/same')(lambda request: Response().text('second'))
