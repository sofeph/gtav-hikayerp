# WebV2 plugin backend

WebV2 serves `data/webv2/` over a loopback-only HTTP server at `http://127.0.0.1:8766/` while the companion is running. It uses a Flask-style route API and Jinja templates (Twig-like `{{ ... }}`, `{% if %}`, `{% for %}`, inheritance and filters). The runtime is local to the game instance; it is not exposed to the LAN.

## Add a plugin

Create `companion/webv2_backend/plugins/my_site/plugin.py`:

```python
from webv2_backend.plugin import PluginBase
from webv2_backend.engine import Response

class MySite(PluginBase):
    name = "my_site"

    def register(self, app):
        @app.get("/api/my-site")
        def index(request):
            return {"message": "Hello from a WebV2 plugin"}

plugin = MySite()
```

A plugin can provide `templates/` beside `plugin.py` and call `app.render_template("my_site/page.html", context)`. Static assets belong in `data/webv2/` or can be returned by a plugin route. Python plugins execute with the same permissions as the companion; only install code you trust.

## TV channels

Edit `data/webv2/tv/channels.json`. Each channel has a `channel_no`, `name`, and an unlisted YouTube `playlist_url`. `program_minutes` sets each scheduled playlist slot in GTA game-time minutes; `start_minute` is the schedule's in-game minute-of-day anchor. The player selects the playlist item and seek offset from the in-game clock when a channel is changed or the TV is reopened. `game_seconds_per_minute` defaults to 120.

Replace the starter `YOUR_..._PLAYLIST_ID` values with real YouTube playlist URLs. Unlisted playlists can be embedded when their videos allow embedding. The URL is provided to the local player endpoint so it can play the playlist; the playlist itself is not displayed as a browseable list.