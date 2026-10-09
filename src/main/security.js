'use strict';

/**
 * Security module: certificate error policy, permission defaults with
 * persistent per-origin grants, external protocol handling.
 */

var { app } = require('electron');

var certDecisions = new Map(); // "<wcId>|<url>" -> "allow"

function keyFor(wcId, url) {
  return wcId + '|' + url;
}

function initSecurity(opts) {
  // opts: { settings, getTabByWcId }
  var settings = opts.settings;

  app.on('certificate-error', function (event, webContents, url, error, certificate, callback) {
    var key = keyFor(webContents.id, url);
    if (certDecisions.get(key) === 'allow') {
      certDecisions.delete(key);
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
    certDecisions.set(keyFor(webContentsId, url), 'allow');
  }

  /* ----------------------------- permissions ---------------------------- */
  function permissionDecision(origin, permission) {
    var perms = settings.get('permissions') || {};
    var byOrigin = perms[origin];
    if (byOrigin && byOrigin[permission] != null) return byOrigin[permission];
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
