'use strict';

/**
 * URL policy shared by every main-process path that turns a URL into an
 * action: address bar, bookmarks, session restore, popups, page-initiated
 * navigation and external protocols. Pure module — unit tested.
 */

var MAX_URL_LEN = 8192;

function asString(url) {
  return String(url == null ? '' : url).trim();
}

function schemeOf(url) {
  var s = asString(url);
  if (!s || s.length > MAX_URL_LEN) return '';
  if (/[\x00-\x1f\x7f]/.test(s)) return ''; // control chars: "java\nscript:" tricks
  var m = /^([a-z][a-z0-9+.\-]*):/i.exec(s);
  return m ? m[1].toLowerCase() + ':' : '';
}

/**
 * URLs the browser may load into a tab from a trusted, user-driven source
 * (address bar, bookmarks, home button, session restore, internal pages).
 * Never javascript:, chrome:, devtools:, blob: or any external protocol.
 */
function isLoadableUrl(url) {
  var s = asString(url);
  var scheme = schemeOf(s);
  if (!scheme) return false;
  if (scheme === 'http:' || scheme === 'https:' || scheme === 'ftp:' || scheme === 'file:') return true;
  if (scheme === 'data:') return true;
  if (scheme === 'about:') return /^about:blank$/i.test(s);
  if (scheme === 'view-source:') return isLoadableUrl(s.slice('view-source:'.length));
  return false;
}

function isWebScheme(scheme) {
  return scheme === 'http:' || scheme === 'https:' || scheme === 'ftp:' || scheme === 'blob:';
}

/**
 * Page-initiated navigation policy (will-navigate, window.open).
 * `opts.fromWeb` = the tab is currently on remote (web) content, so local
 * file targets must be refused.
 */
function isAllowedNavigation(url, opts) {
  var s = asString(url);
  var scheme = schemeOf(s);
  var fromWeb = !!(opts && opts.fromWeb);
  if (!scheme) return false;
  if (scheme === 'http:' || scheme === 'https:' || scheme === 'ftp:') return true;
  if (scheme === 'about:') return /^about:blank$/i.test(s);
  if (scheme === 'blob:') return fromWeb; // same-origin enforced by Chromium
  if (scheme === 'file:') return !fromWeb;
  if (scheme === 'view-source:') return isAllowedNavigation(s.slice('view-source:'.length), opts);
  return false;
}

/** True when a tab URL counts as remote web content. */
function isWebUrl(url) {
  return isWebScheme(schemeOf(url));
}

/** Strict external-protocol gate for shell.openExternal (mailto/tel only). */
function isSafeExternalUrl(url) {
  var s = String(url == null ? '' : url);
  if (!s || s.length > 2048) return false;
  if (/^[\s]|[\s]$/.test(s)) return false;
  if (/[\x00-\x1f\x7f]/.test(s)) return false;
  var m = /^(mailto|tel):/i.exec(s);
  if (!m) return false;
  return s.slice(m[0].length).length > 0;
}

/** URLs that may be stored as favorites. */
function isBookmarkUrl(url) {
  var scheme = schemeOf(url);
  return scheme === 'http:' || scheme === 'https:' || scheme === 'ftp:' || scheme === 'file:';
}

/** Search-engine URL templates must be plain http(s). */
function isSearchTemplate(template) {
  var s = asString(template);
  return s.length > 0 && s.length <= 500 && /^https?:\/\//i.test(s);
}

/** Filter a stored session list down to restorable URLs. */
function sanitizeSessionList(list, max) {
  if (!Array.isArray(list)) return [];
  var limit = Math.max(1, Math.min(max || 50, 200));
  var out = [];
  for (var i = 0; i < list.length && out.length < limit; i++) {
    var u = list[i];
    if (typeof u === 'string' && isLoadableUrl(u)) out.push(u);
  }
  return out;
}

module.exports = {
  MAX_URL_LEN: MAX_URL_LEN,
  schemeOf: schemeOf,
  isLoadableUrl: isLoadableUrl,
  isAllowedNavigation: isAllowedNavigation,
  isWebUrl: isWebUrl,
  isSafeExternalUrl: isSafeExternalUrl,
  isBookmarkUrl: isBookmarkUrl,
  isSearchTemplate: isSearchTemplate,
  sanitizeSessionList: sanitizeSessionList
};
