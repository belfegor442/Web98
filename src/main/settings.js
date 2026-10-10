'use strict';

/**
 * Settings module: defaults, persistence via store, change notification.
 */

var path = require('path');
var { pathToFileURL } = require('url');
var { isLoadableUrl, isSearchTemplate } = require('../shared/safeurl');
var validate = require('../shared/validate');

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
      cache.permissions = sanitizePermissions(saved[k]);
    } else if (k === 'lastSession') {
      // restored separately through sanitizeSessionList in windows.js
      cache.lastSession = saved[k];
    } else if (k === 'homepage') {
      // Saved homepages must remain loadable URLs.
      if (typeof saved[k] === 'string' && isLoadableUrl(saved[k])) cache.homepage = saved[k];
    } else if (k === 'engines') {
      // merged below
    } else {
      cache[k] = saved[k];
    }
  });
  // Preserve built-in engines, append user-defined ones (http/https only).
  var baseNames = defaults(appPaths).engines.map(function (e) { return e.name; });
  if (Array.isArray(saved.engines)) {
    var extras = validate.sanitizeEngines(saved.engines) || [];
    var extra = extras.filter(function (e) { return baseNames.indexOf(e.name) === -1; });
    cache.engines = defaults(appPaths).engines.concat(extra);
  }
  cache.zoom = validate.clampNumber(cache.zoom, 0.25, 3, 1);
  if (typeof cache.searchEngine !== 'string' || !cache.searchEngine) {
    cache.searchEngine = 'DuckDuckGo';
  }
  if (cache.downloadsDir != null &&
      (typeof cache.downloadsDir !== 'string' || !path.isAbsolute(cache.downloadsDir))) {
    cache.downloadsDir = null;
  }
  ['sessionRestore', 'statusBar', 'toolbarVisible'].forEach(function (k) {
    cache[k] = !!cache[k];
  });
  if (!Array.isArray(cache.engines) || !cache.engines.length) {
    cache.engines = defaults(appPaths).engines;
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
    var template = engine && engine.template;
    if (!isSearchTemplate(template)) {
      template = cache.engines[0] && cache.engines[0].template;
    }
    if (!isSearchTemplate(template)) {
      template = 'https://duckduckgo.com/?q=%s';
    }
    return template;
  }

  function onChange(fn) {
    listeners.push(fn);
  }

  return { get: get, set: set, searchTemplate: searchTemplate, onChange: onChange };
}

/** Drop malformed origin/permission entries from persisted permissions. */
function sanitizePermissions(perms) {
  var out = {};
  if (!perms || typeof perms !== 'object') return out;
  Object.keys(perms).forEach(function (origin) {
    if (!validate.isGrantOrigin(origin)) return;
    var byOrigin = perms[origin];
    if (!byOrigin || typeof byOrigin !== 'object') return;
    Object.keys(byOrigin).forEach(function (permission) {
      if (!validate.isPermissionName(permission)) return;
      var value = byOrigin[permission];
      if (typeof value !== 'boolean') return;
      if (!out[origin]) out[origin] = {};
      out[origin][permission] = value;
    });
  });
  return out;
}

module.exports = { createSettings: createSettings, defaults: defaults, sanitizePermissions: sanitizePermissions };
