'use strict';

/**
 * Input validation for IPC payloads and imported data. Pure module —
 * unit tested. Everything returns sanitized values or a rejection marker
 * instead of throwing.
 */

var safeurl = require('./safeurl');

function isPlainObject(v) {
  return !!v && typeof v === 'object' && !Array.isArray(v);
}

function clampNumber(v, min, max, dflt) {
  var n = Number(v);
  if (!Number.isFinite(n)) return dflt;
  return Math.min(max, Math.max(min, n));
}

function sanitizeId(v) {
  var n = Number(v);
  return Number.isInteger(n) && n >= 0 ? n : null;
}

/** Strip control characters and cap length; non-strings collapse to ''. */
function sanitizeText(v, max) {
  if (typeof v !== 'string') return '';
  return v.replace(/[\x00-\x1f\x7f]/g, '').trim().slice(0, max || 200);
}

/**
 * Filesystem-safe download filename: no directories, no reserved
 * characters, no dot-only names, no Windows reserved device names,
 * no trailing dots/spaces (Windows rejects those on create).
 */
function sanitizeFilename(name) {
  var s = typeof name === 'string' ? name : '';
  s = s.replace(/[\x00-\x1f\x7f]/g, '');
  // Strip any path components from both separator styles.
  var parts = s.split(/[\\/]+/);
  s = parts[parts.length - 1];
  s = s.replace(/[<>:"|?*]/g, '_').replace(/^\.+/, '').trim();
  s = s.replace(/[. ]+$/g, ''); // Windows drops trailing dots/spaces silently
  if (!s) s = 'download';
  var stem = s.split('.')[0];
  if (/^(con|prn|aux|nul|com[1-9]|lpt[1-9])$/i.test(stem)) s = '_' + s;
  if (s.length > 120) {
    var ext = (s.match(/\.[A-Za-z0-9]{1,12}$/) || [''])[0];
    s = s.slice(0, 120 - ext.length) + ext;
  }
  return s;
}

/** Sanitize a single bookmark entry. Returns null when unusable. */
function sanitizeBookmark(raw) {
  if (!isPlainObject(raw)) return null;
  var url = typeof raw.url === 'string' ? raw.url.trim() : '';
  if (url && !safeurl.isBookmarkUrl(url)) return null; // non-empty urls must be bookmarkable; empty = folder
  var parent = sanitizeId(raw.parent);
  if (parent == null) parent = 0;
  var name = sanitizeText(raw.name, 200) || url || 'Favorite';
  return { name: name, url: url, parent: parent };
}

/** Sanitize an imported bookmark array; bad rows are skipped. */
function sanitizeBookmarkList(raw, maxRows) {
  if (!Array.isArray(raw)) return [];
  var limit = Math.max(1, Math.min(maxRows || 5000, 20000));
  var out = [];
  for (var i = 0; i < raw.length && out.length < limit; i++) {
    var b = sanitizeBookmark(raw[i]);
    if (b) out.push(b);
  }
  return out;
}

/**
 * Validate a settings patch from a renderer. Only known keys with
 * well-formed values survive; unknown or dangerous values are rejected.
 */
function sanitizeSettingsPatch(patch) {
  var out = {};
  var rejected = [];
  if (!isPlainObject(patch)) return { patch: out, rejected: rejected };

  Object.keys(patch).forEach(function (key) {
    var v = patch[key];
    switch (key) {
      case 'homepage':
        if (typeof v === 'string' && safeurl.isLoadableUrl(v)) out.homepage = v;
        else rejected.push(key);
        break;
      case 'searchEngine':
        if (typeof v === 'string' && v.trim() && v.length <= 50) out.searchEngine = v.trim();
        else rejected.push(key);
        break;
      case 'engines': {
        var list = sanitizeEngines(v);
        if (list && list.length) out.engines = list;
        else if (Array.isArray(v)) rejected.push(key);
        break;
      }
      case 'sessionRestore':
      case 'statusBar':
      case 'toolbarVisible':
        if (typeof v === 'boolean') out[key] = v;
        else rejected.push(key);
        break;
      case 'zoom':
        out.zoom = clampNumber(v, 0.25, 3, 1);
        if (!Number.isFinite(Number(v))) rejected.push(key);
        break;
      case 'downloadsDir':
        if (v == null) out.downloadsDir = null;
        else if (typeof v === 'string' && v.length <= 512 && !/\x00/.test(v)) out.downloadsDir = v;
        else rejected.push(key);
        break;
      default:
        rejected.push(key); // includes permissions, lastSession, etc.
    }
  });
  return { patch: out, rejected: rejected };
}

/** Keep only well-formed, http(s)-based search engines. */
function sanitizeEngines(list) {
  if (!Array.isArray(list)) return null;
  var out = [];
  for (var i = 0; i < list.length && out.length < 16; i++) {
    var e = list[i];
    if (!isPlainObject(e)) continue;
    var name = sanitizeText(e.name, 50);
    if (!name || !safeurl.isSearchTemplate(e.template)) continue;
    out.push({ name: name, template: String(e.template) });
  }
  return out;
}

function isGrantOrigin(origin) {
  if (typeof origin !== 'string') return false;
  if (origin === 'null') return true;
  return /^https?:\/\/[^\s]{1,300}$/.test(origin);
}

function isPermissionName(permission) {
  return typeof permission === 'string' && /^[a-z][a-z0-9\-]{0,63}$/i.test(permission);
}

module.exports = {
  isPlainObject: isPlainObject,
  clampNumber: clampNumber,
  sanitizeId: sanitizeId,
  sanitizeText: sanitizeText,
  sanitizeFilename: sanitizeFilename,
  sanitizeBookmark: sanitizeBookmark,
  sanitizeBookmarkList: sanitizeBookmarkList,
  sanitizeSettingsPatch: sanitizeSettingsPatch,
  sanitizeEngines: sanitizeEngines,
  isGrantOrigin: isGrantOrigin,
  isPermissionName: isPermissionName
};
