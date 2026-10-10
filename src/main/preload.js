'use strict';

var { contextBridge, ipcRenderer } = require('electron');

// Strict channel allowlists: compromised chrome/dialog scripts can only
// reach the documented surface, nothing else.
var INVOKE_CHANNELS = [
  'app:command', 'app:find', 'app:open-url', 'app:stats', 'app:view-source',
  'bookmarks:add', 'bookmarks:export', 'bookmarks:import', 'bookmarks:list',
  'bookmarks:remove', 'bookmarks:update',
  'chrome:bounds',
  'downloads:cancel', 'downloads:clearFinished', 'downloads:list',
  'downloads:open', 'downloads:pause', 'downloads:remove', 'downloads:resume',
  'downloads:showFolder',
  'history:clear', 'history:delete', 'history:list',
  'nav:back', 'nav:forward', 'nav:go', 'nav:home', 'nav:reload', 'nav:stop',
  'omni:suggest',
  'settings:clearData', 'settings:get', 'settings:grants', 'settings:revoke',
  'settings:set',
  'tabs:activate', 'tabs:close', 'tabs:create', 'tabs:list',
  'win:close', 'win:isMax', 'win:max', 'win:min', 'win:title',
  'window:open'
];
var SEND_CHANNELS = ['chrome:popup', 'ctx:command'];
var ON_CHANNELS = [
  'app:ready',
  'bookmarks:changed',
  'chrome:add-favorite', 'chrome:find', 'chrome:focus-address',
  'chrome:tab-focused',
  'ctx',
  'downloads',
  'history:changed',
  'nav',
  'payload',
  'settings:changed',
  'tabs',
  'win:active', 'win:state'
];

contextBridge.exposeInMainWorld('web98', {
  versions: process.versions,
  invoke: function (channel, data) {
    if (INVOKE_CHANNELS.indexOf(channel) === -1) {
      return Promise.reject(new Error('Blocked IPC channel: ' + channel));
    }
    return ipcRenderer.invoke(channel, data);
  },
  send: function (channel, data) {
    if (SEND_CHANNELS.indexOf(channel) === -1) return;
    ipcRenderer.send(channel, data);
  },
  on: function (channel, callback) {
    if (ON_CHANNELS.indexOf(channel) === -1) {
      return function () {};
    }
    var listener = function (_event, payload) {
      callback(payload);
    };
    ipcRenderer.on(channel, listener);
    return function () {
      ipcRenderer.removeListener(channel, listener);
    };
  }
});
