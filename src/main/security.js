'use strict';

/**
 * Security module: certificate error policy, permission defaults with
 * persistent per-origin grants, external protocol handling.
 */

var { app } = require('electron');
var validate = require('../shared/validate');

var certDecisions = new Map(); // "<wcId>|<url>" -> expiry timestamp (ms)
var CERT_GRANT_TTL_MS = 5 * 60 * 1000;

function keyFor(wcId, url) {
  return wcId + '|' + url;
}

function pruneCertDecisions(now) {
  Array.from(certDecisions.entries()).forEach(function (entry) {
    if (entry[1] <= now) certDecisions.delete(entry[0]);
  });
}

function initSecurity(opts) {
  // opts: { settings, getTabByWcId }
  var settings = opts.settings;

  app.on('certificate-error', function (event, webContents, url, error, certificate, callback) {
    var key = keyFor(webContents.id, url);
    var now = Date.now();
    pruneCertDecisions(now);
    var decision = certDecisions.get(key);
    if (decision > now) {
      // Grant valid for its TTL: Chromium can raise certificate-error more
      // than once for a single load (preconnect + main request), so the
      // grant must outlive the first event and then expire on its own.
      event.preventDefault();
      callback(true);
      return;
    }
    // Default: block. The tab shows a certificate error page with an
    // explicit "continue anyway" action.
    event.preventDefault();
    callback(false);
  });

  function grantCert(webContentsId, url) {
    if (!Number.isInteger(webContentsId)) return;
    if (typeof url !== 'string' || !/^https?:\/\//i.test(url)) return;
    pruneCertDecisions(Date.now());
    certDecisions.set(keyFor(webContentsId, url), Date.now() + CERT_GRANT_TTL_MS);
  }

  /* ----------------------------- permissions ---------------------------- */
  function permissionDecision(origin, permission) {
    if (typeof origin !== 'string' || !validate.isPermissionName(permission)) return 'deny';
    var perms = settings.get('permissions') || {};
    var byOrigin = perms[origin];
    if (byOrigin && typeof byOrigin[permission] === 'boolean') {
      return byOrigin[permission] ? 'allow' : 'deny';
    }
    if (permission === 'fullscreen') return 'allow';
    return 'deny';
  }

  sessionDefaults();
  function sessionDefaults() {
    var ses = opts.session;
    ses.setPermissionRequestHandler(function (webContents, permission, callback) {
      var origin = safeOrigin(webContents.getURL());
      callback(permissionDecision(origin, permission) === 'allow');
    });
    ses.setPermissionCheckHandler(function (webContents, permission, requestingOrigin) {
      var origin = safeOrigin(requestingOrigin || webContents.getURL());
      return permissionDecision(origin, permission) === 'allow';
    });
  }

  function safeOrigin(url) {
    try {
      return new URL(url).origin;
    } catch (e) {
      return 'null';
    }
  }

  function listGrants() {
    var perms = settings.get('permissions') || {};
    var out = [];
    Object.keys(perms).forEach(function (origin) {
      Object.keys(perms[origin]).forEach(function (permission) {
        out.push({ origin: origin, permission: permission, value: perms[origin][permission] });
      });
    });
    return out;
  }

  function setGrant(origin, permission, value) {
    if (!validate.isGrantOrigin(origin) || !validate.isPermissionName(permission)) return;
    if (value != null && typeof value !== 'boolean') return;
    var perms = Object.assign({}, settings.get('permissions') || {});
    var byOrigin = Object.assign({}, perms[origin] || {});
    if (value == null) {
      delete byOrigin[permission];
    } else {
      byOrigin[permission] = value;
    }
    if (Object.keys(byOrigin).length === 0) {
      delete perms[origin];
    } else {
      perms[origin] = byOrigin;
    }
    settings.set('permissions', perms);
  }

  return {
    grantCert: grantCert,
    listGrants: listGrants,
    setGrant: setGrant
  };
}

module.exports = { initSecurity: initSecurity };
