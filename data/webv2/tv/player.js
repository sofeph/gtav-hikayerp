'use strict';

const API = 'http://127.0.0.1:8766/api/tv/channels';
const elements = {
  player: document.querySelector('#youtube-player'),
  empty: document.querySelector('#empty'),
  status: document.querySelector('#status'),
  number: document.querySelector('#channel-number'),
  name: document.querySelector('#channel-name'),
  title: document.querySelector('#program-title'),
  message: document.querySelector('#message'),
  volume: document.querySelector('#volume'),
};
let channels = [];
let selected = 0;
let gameMinute = 0;
let secondsPerGameMinute = 120;
let player = null;
let playerReady = false;
let loadedChannel = -1;
let loadedPlaylistId = '';
let lastProgramIndex = -1;

const initialTime = new URLSearchParams(window.location.search);
if (initialTime.has('hour') && initialTime.has('minute')) {
  gameMinute = ((Number(initialTime.get('hour')) || 0) % 24) * 60 +
    ((Number(initialTime.get('minute')) || 0) % 60);
}

function playlistId(value) {
  try {
    const url = new URL(value);
    if (!['youtube.com', 'www.youtube.com', 'm.youtube.com'].includes(url.hostname) || url.protocol !== 'https:') return '';
    return url.searchParams.get('list') || '';
  } catch {
    return '';
  }
}

function channelPosition(channel) {
  const start = ((Number(channel.start_minute) % 1440) + 1440) % 1440;
  const elapsed = (gameMinute - start + 1440) % 1440;
  const slot = Math.max(1, Number(channel.program_minutes) || 30);
  return { index: Math.floor(elapsed / slot), seconds: (elapsed % slot) * secondsPerGameMinute };
}

function setStatus(text) {
  elements.status.textContent = text;
  elements.message.textContent = text;
}

function updateBanner() {
  const channel = channels[selected];
  if (!channel) return;
  elements.number.textContent = String(channel.channel_no).padStart(2, '0');
  elements.name.textContent = channel.name;
  elements.title.textContent = `Program süresi: ${channel.program_minutes || 30} oyun dakikası`;
}

function loadSelected(force = false) {
  if (!playerReady || channels.length === 0) return;
  const channel = channels[selected];
  const listId = playlistId(channel.playlist_url);
  if (!listId || listId.startsWith('YOUR_')) {
    elements.empty.hidden = false;
    setStatus('Kanal listesi için data/webv2/tv/channels.json içindeki playlist_url alanını düzenle.');
    return;
  }
  const position = channelPosition(channel);
  const playlist = player.getPlaylist() || [];
  if (loadedPlaylistId !== listId || playlist.length === 0) {
    loadedPlaylistId = listId;
    player.cuePlaylist(listId);
    return;
  }
  const itemIndex = playlist.length ? position.index % playlist.length : position.index;
  if (!force && loadedChannel === selected && lastProgramIndex === position.index) return;
  loadedChannel = selected;
  lastProgramIndex = position.index;
  elements.empty.hidden = true;
  elements.message.textContent = '';
  player.loadPlaylist(listId, itemIndex, position.seconds);
  player.setVolume(Number(elements.volume.value));
  player.playVideo();
  updateBanner();
}

function selectChannel(index) {
  if (!channels.length) return;
  selected = (index + channels.length) % channels.length;
  localStorage.setItem('hikaye-tv-channel', String(channels[selected].channel_no));
  lastProgramIndex = -1;
  updateBanner();
  loadSelected(true);
}

function onYouTubeIframeAPIReady() {
  player = new YT.Player('youtube-player', {
    width: '100%',
    height: '100%',
    playerVars: { autoplay: 1, controls: 1, playsinline: 1, rel: 0, modestbranding: 1 },
    events: {
      onReady() {
        playerReady = true;
        loadSelected(true);
      },
      onStateChange(event) {
        if (event.data === YT.PlayerState.CUED) loadSelected(true);
      },
      onError(event) {
        elements.empty.hidden = false;
        setStatus(`YouTube yayını açılamadı (hata ${event.data}). Playlist erişimini kontrol et.`);
      },
    },
  });
}
window.onYouTubeIframeAPIReady = onYouTubeIframeAPIReady;

function loadYouTubeApi() {
  if (document.querySelector('#youtube-api')) return;
  const script = document.createElement('script');
  script.id = 'youtube-api';
  script.src = 'https://www.youtube.com/iframe_api';
  script.onerror = () => setStatus('YouTube oynatıcı API yüklenemedi. İnternet bağlantısını kontrol et.');
  document.head.appendChild(script);
}

window.setTvGameTime = (hour, minute) => {
  gameMinute = ((Number(hour) || 0) % 24) * 60 + ((Number(minute) || 0) % 60);
  const channel = channels[selected];
  if (!channel || !playerReady) return;
  const position = channelPosition(channel);
  if (position.index !== lastProgramIndex) loadSelected(true);
};

window.setTvVolume = (value) => {
  elements.volume.value = String(Math.max(0, Math.min(100, Number(value) * 100)));
  if (playerReady) player.setVolume(Number(elements.volume.value));
};

document.querySelector('#previous').addEventListener('click', () => selectChannel(selected - 1));
document.querySelector('#next').addEventListener('click', () => selectChannel(selected + 1));
elements.volume.addEventListener('input', () => {
  if (playerReady) player.setVolume(Number(elements.volume.value));
});
document.addEventListener('keydown', (event) => {
  if (event.key === 'ArrowUp' || event.key === 'PageUp') {
    event.preventDefault();
    selectChannel(selected - 1);
  } else if (event.key === 'ArrowDown' || event.key === 'PageDown') {
    event.preventDefault();
    selectChannel(selected + 1);
  }
});

fetch(API, { cache: 'no-store' })
  .then((response) => {
    if (!response.ok) throw new Error(`Kanal servisi ${response.status}`);
    return response.json();
  })
  .then((data) => {
    channels = Array.isArray(data.channels) ? data.channels : [];
    secondsPerGameMinute = Math.max(1, Number(data.game_seconds_per_minute) || 120);
    if (!channels.length) {
      setStatus('Kanal bulunamadı. data/webv2/tv/channels.json dosyasını düzenle.');
      return;
    }
    const savedNumber = Number(localStorage.getItem('hikaye-tv-channel'));
    const savedIndex = channels.findIndex((channel) => Number(channel.channel_no) === savedNumber);
    selected = savedIndex >= 0 ? savedIndex : 0;
    updateBanner();
    loadYouTubeApi();
  })
  .catch((error) => {
    setStatus(`Kanal listesi yüklenemedi: ${error.message}`);
  });
