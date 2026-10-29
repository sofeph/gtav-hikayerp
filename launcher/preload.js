'use strict';
const { contextBridge, ipcRenderer } = require('electron');

const invoke = (ch) => (...a) => ipcRenderer.invoke(ch, ...a);
const on = (ch) => (cb) => ipcRenderer.on(ch, (_e, p) => cb(p));

contextBridge.exposeInMainWorld('api', {
  init: invoke('init'),
  saveSettings: invoke('save-settings'),
  pickGameDir: invoke('pick-game-dir'),
  install: invoke('install'),
  importVendor: invoke('import-vendor'),
  installAddon: invoke('install-addon'),
  listAddons: invoke('list-addons'),
  removeAddon: invoke('remove-addon'),
  autoSetup: invoke('auto-setup'),
  uninstall: invoke('uninstall'),
  togglePatch: invoke('toggle-patch'),
  installTrPatch: invoke('install-tr-patch'),
  start: invoke('start'),
  stop: invoke('stop'),
  checkUpdate: invoke('check-update'),
  applyUpdate: invoke('apply-update'),
  open: invoke('open'),
  openUrl: invoke('open-url'),
  copyText: invoke('copy-text'),
  relaunch: invoke('relaunch'),
  onLog: on('log'),
  onState: on('state'),
  onProgress: on('progress'),
  onStep: on('step'),
  onUpdateInfo: on('update-info'),
});
