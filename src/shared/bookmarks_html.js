'use strict';

/**
 * Netscape bookmark file (classic IE "favorites.htm") export/import.
 * Pure module — mirrors native/src/bookmarks_html.cpp. Unit tested.
 */

var MAX_NODES = 10000;
var MAX_NAME = 512;
var MAX_URL = 8192;

function escapeHtml(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
  });
}

function decodeEntities(s) {
  return String(s == null ? '' : s).replace(/&(#x[0-9a-f]+|#[0-9]+|[a-z]+);/gi, function (m, ent) {
    var e = ent.toLowerCase();
    if (e === 'amp') return '&';
    if (e === 'lt') return '<';
    if (e === 'gt') return '>';
    if (e === 'quot') return '"';
    if (e === 'apos' || e === '#39' || e === '#x27') return "'";
    if (e === 'nbsp') return ' ';
    if (e.charAt(0) === '#') {
      var code;
      if (e.charAt(1) === 'x') {
        code = parseInt(e.slice(2), 16);
      } else {
        code = parseInt(e.slice(1), 10);
      }
      if (Number.isFinite(code) && code > 0 && code <= 0x10ffff) {
        try { return String.fromCodePoint(code); } catch (err) { return m; }
      }
    }
    return m;
  });
}

/** rows: [{id, parent, name, url}] — url '' means folder. Returns HTML string. */
function exportNetscapeHtml(rows) {
  var byId = {};
  var kids = {};
  var i;
  for (i = 0; i < rows.length; i++) byId[rows[i].id] = rows[i];
  for (i = 0; i < rows.length; i++) {
    var r = rows[i];
    var p = (r.parent !== r.id && byId[r.parent]) ? r.parent : 0;
    (kids[p] = kids[p] || []).push(r);
  }
  var out =
    '<!DOCTYPE NETSCAPE-ABookmark-file-1>\n' +
    '<!-- This is an automatically generated file.\n' +
    '     It will be read and overwritten.\n' +
    '     DO NOT EDIT! -->\n' +
    '<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">\n' +
    '<TITLE>Bookmarks</TITLE>\n' +
    '<H1>Bookmarks</H1>\n' +
    '<DL><p>\n';
  (function walk(parent, depth) {
    var list = kids[parent];
    if (!list) return;
    var pad = new Array(depth + 1).join('    ');
    for (var k = 0; k < list.length; k++) {
      var n = list[k];
      if (!n.url) {
        out += pad + '<DT><H3>' + escapeHtml(n.name) + '</H3>\n';
        out += pad + '<DL><p>\n';
        walk(n.id, depth + 1);
        out += pad + '</DL><p>\n';
      } else {
        out += pad + '<DT><A HREF="' + escapeHtml(n.url) + '">' +
          escapeHtml(n.name) + '</A>\n';
      }
    }
  })(0, 1);
  out += '</DL><p>\n';
  return out;
}

function findTagEnd(s, lt) {
  var quote = null;
  for (var i = lt + 1; i < s.length; i++) {
    var c = s.charAt(i);
    if (quote) {
      if (c === quote) quote = null;
      continue;
    }
    if (c === '"' || c === "'") { quote = c; continue; }
    if (c === '>') return i;
  }
  return -1;
}

function attrValue(tag, name) {
  var re = new RegExp('(?:^|[\\s"\'/])' + name.replace(/[-\/\\^$*+?.()|[\]{}]/g, '\\$&') + '\\s*=\\s*("([^"]*)"|\'([^\']*)\'|([^\\s"\'=<>`]+))', 'i');
  var m = re.exec(tag);
  if (!m) return '';
  return decodeEntities(m[2] != null ? m[2] : (m[3] != null ? m[3] : m[4])) || '';
}

function readText(html, state, closeTag) {
  var re = new RegExp(closeTag, 'i');
  var rest = html.slice(state.i);
  var m = re.exec(rest);
  var text;
  if (!m) {
    text = rest;
    state.i = html.length;
  } else {
    text = rest.slice(0, m.index);
    state.i = state.i + m.index + m[0].length;
  }
  return decodeEntities(text);
}

/**
 * Parse a Netscape bookmark file.
 * Returns { ok, nodes: [{name, url, children:[...]}], error? } —
 * url '' means folder. ok=false when nothing usable was found.
 */
function parseNetscapeHtml(html) {
  html = String(html == null ? '' : html);
  var root = { name: '', url: '', children: [] };
  var stack = [root];
  var pendingFolder = -1; // index into stack top's children
  var nodes = 0;
  var state = { i: 0 };
  var n = html.length;

  while (state.i < n) {
    var lt = html.indexOf('<', state.i);
    if (lt === -1) break;
    if (html.slice(lt, lt + 4).toLowerCase() === '<!--') {
      var end = html.indexOf('-->', lt + 4);
      state.i = end === -1 ? n : end + 3;
      continue;
    }
    var gt = findTagEnd(html, lt);
    if (gt === -1) break;
    var raw = html.slice(lt + 1, gt);
    state.i = gt + 1;
    var closing = false;
    if (raw.charAt(0) === '/') { closing = true; raw = raw.slice(1); }
    var tok = (raw.split(/[\s\/>]/)[0] || '').toLowerCase();
    if (tok === 'dl') {
      if (closing) {
        if (stack.length > 1) { stack.pop(); pendingFolder = -1; }
      } else if (pendingFolder !== -1) {
        stack.push(stack[stack.length - 1].children[pendingFolder]);
        pendingFolder = -1;
      } else {
        stack.push(stack[stack.length - 1]);
      }
      continue;
    }
    if (closing) continue;
    var top = stack[stack.length - 1];
    if (tok === 'h3') {
      var folderName = readText(html, state, '<\\/H3>').trim();
      pendingFolder = -1;
      if (nodes < MAX_NODES) {
        top.children.push({ name: folderName.slice(0, MAX_NAME), url: '', children: [] });
        pendingFolder = top.children.length - 1;
        nodes++;
      }
      continue;
    }
    if (tok === 'a') {
      var href = attrValue(raw, 'href');
      var linkName = readText(html, state, '<\\/A>').trim();
      pendingFolder = -1;
      if (nodes < MAX_NODES && href) {
        top.children.push({ name: linkName.slice(0, MAX_NAME), url: href.slice(0, MAX_URL), children: [] });
        nodes++;
      }
      continue;
    }
  }
  return { ok: root.children.length > 0, nodes: root.children };
}

module.exports = {
  escapeHtml: escapeHtml,
  decodeEntities: decodeEntities,
  exportNetscapeHtml: exportNetscapeHtml,
  parseNetscapeHtml: parseNetscapeHtml
};
