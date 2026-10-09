'use strict';

/**
 * Settings module: defaults, persistence via store, change notification.
 */

var path = require('path');
var { pathToFileURL } = require('url');

function defaults(appPaths) {
  return {
    homepage: pathToFileURL(path.join(appPaths.renderer, 'startpage.html')).href,
    searchEngine: 'DuckDuckGo',
    engines: [
      { name: 'DuckDuckGo', template: 'https://duckduckgo.com/?q=%s' },
      { name: 'Google', template: 'https://www.google.com/search?q=%s' },
      { name: 'Bing', template: 'https://www.bing.com/search?q=%s' },
      { name: 'Wikipedia', template: 'https://en.wikipedia.org/w/index.php?search=%s' }
    ],
    sessionRestore: true,
    statusBar: true,
    toolbarVisible: true,
    zoom: 1,
    downloadsDir: null,
    permissions: {},
    lastSession: null
  };
}

function toSlash(p) {
  return p.replace(/\\/g, '/');
}

function createSettings(store, appPaths) {
  var cache = defaults(appPaths);
  var saved = store.allSettings();
  Object.keys(saved).forEach(function (k) {
    if (k === 'permissions' && saved[k] && typeof saved[k] === 'object') {
      cache.permissions = Object.assign(cache.permissions || {}, saved[k]);
    } else {
      cache[k] = saved[k];
    }
  });
  // Preserve built-in engines, append user-defined ones.
  var baseNames = defaults(appPaths).engines.map(function (e) { return e.name; });
  (cache.engines || []).forEach(function () {});
  if (Array.isArray(cache.engines)) {
    var extra = cache.engines.filter(function (e) { return baseNames.indexOf(e.name) === -1; });
    cache.engines = defaults(appPaths).engines.concat(extra);
  }

  var listeners = [];

  function get(key) {
    if (key == null) return Object.assign({}, cache);
    return cache[key];
  }

  function set(key, value) {
    cache[key] = value;
    store.setSetting(key, value);
    listeners.forEach(function (fn) { fn(key, value); });
    return value;
  }

  function searchTemplate() {
    var engine = (cache.engines || []).find(function (e) {
      return e.name === cache.searchEngine;
    });
    return engine ? engine.template : cache.engines[0].template;
  }

  function onChange(fn) {
    listeners.push(fn);
  }

  return { get: get, set: set, searchTemplate: searchTemplate, onChange: onChange };
}

module.exports = { createSettings: createSettings, defaults: defaults };
