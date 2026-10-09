'use strict';

/**
 * Address bar parsing: decides whether input is a URL, a search query,
 * a javascript: command, or empty. Pure module — unit tested.
 */

var KNOWN_SCHEMES = [
  'http',
  'https',
  'file',
  'ftp',
  'about',
  'mailto',
  'tel',
  'javascript',
  'view-source',
  'data'
];

var SCHEME_RE = /^([a-z][a-z0-9+.-]*):/i;
var IPV4_RE = /^\d{1,3}(\.\d{1,3}){3}$/;
var HOSTISH_RE = /^[a-z0-9][a-z0-9-]*(\.[a-z0-9-]+)+$/i;

function isKnownScheme(scheme) {
  return KNOWN_SCHEMES.indexOf(String(scheme).toLowerCase()) !== -1;
}

/**
 * @param {string} raw user input
 * @param {{searchTemplate?: string, defaultScheme?: string}} [opts]
 * @returns {{type:'empty'|'url'|'search'|'javascript', url?:string, query?:string, term?:string}}
 */
function parseAddress(raw, opts) {
  opts = opts || {};
  var s = String(raw == null ? '' : raw).trim();
  if (!s) return { type: 'empty' };

  var schemeMatch = SCHEME_RE.exec(s);
  if (schemeMatch && isKnownScheme(schemeMatch[1])) {
    var scheme = schemeMatch[1].toLowerCase();
    if (scheme === 'javascript') {
      return { type: 'javascript', url: s.slice(schemeMatch[0].length) };
    }
    if (scheme === 'about' && !/^about:blank$/i.test(s)) {
      // Unknown about: page — treat like a host-less search term.
      return finishSearch(s, opts);
    }
    if (scheme === 'view-source') {
      return { type: 'url', url: s };
    }
    if (scheme === 'http' || scheme === 'https' || scheme === 'file' || scheme === 'ftp') {
      return { type: 'url', url: s };
    }
    return { type: 'url', url: s }; // mailto:, tel:, data: pass through to handlers
  }

  // No usable scheme: decide between URL and search.
  if (/\s/.test(s)) return finishSearch(s, opts);

  var authority = s.split('/')[0];
  var hostPart = authority.split(':')[0];
  var hasPort = authority.indexOf(':') !== -1;

  if (s.indexOf('localhost') === 0 || IPV4_RE.test(hostPart)) {
    return { type: 'url', url: (opts.defaultScheme || 'http') + '://' + s };
  }
  if (HOSTISH_RE.test(hostPart)) {
    return { type: 'url', url: (opts.defaultScheme || 'http') + '://' + s };
  }
  // "example" or "foo.bar baz"-less garbage without a dot: search.
  if (!hasPort && hostPart.indexOf('.') === -1) return finishSearch(s, opts);

  return finishSearch(s, opts);
}

function finishSearch(term, opts) {
  var template =
    opts.searchTemplate || 'https://duckduckgo.com/?q=%s';
  var url = template.indexOf('%s') !== -1
    ? template.replace('%s', encodeURIComponent(term))
    : template + encodeURIComponent(term);
  return { type: 'search', query: term, url: url };
}

module.exports = { parseAddress: parseAddress, isKnownScheme: isKnownScheme };
