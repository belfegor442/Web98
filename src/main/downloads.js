'use strict';

/**
 * Download manager: tracks Electron downloads, exposes pause/resume/cancel,
 * safe file opening and folder reveal. State is pushed to renderers.
 */

var path = require('path');
var fs = require('fs');
var { shell, dialog, app } = require('electron');
var { sanitizeFilename } = require('../shared/validate');

var DANGEROUS_EXT = [
  '.exe', '.msi', '.bat', '.cmd', '.com', '.scr', '.pif',
  '.js', '.jse', '.vbs', '.vbe', '.wsf', '.ps1', '.lnk', '.reg', '.hta'
];

function uniquePath(dir, name) {
  var base = path.join(dir, name);
  if (!fs.existsSync(base)) return base;
  var ext = path.extname(name);
  var stem = path.basename(name, ext);
  var i = 2;
  while (fs.existsSync(path.join(dir, stem + ' (' + i + ')' + ext))) i++;
  return path.join(dir, stem + ' (' + i + ')' + ext);
}

function createDownloads(opts) {
  // opts: { session, emit, downloadsDir }
  var items = [];
  var nextId = 1;
  var MAX_FINISHED = 200;
  var pushScheduled = false;

  function snapshot() {
    return items.map(function (d) {
      return {
        id: d.id,
        filename: d.filename,
        savePath: d.savePath,
        url: d.url,
        state: d.state,
        received: d.received,
        total: d.total,
        startTime: d.startTime,
        endTime: d.endTime
      };
    });
  }

  // Progress updates arrive in bursts; coalesce so a busy download cannot
  // flood the renderer with one full-list IPC per network event.
  function push() {
    if (pushScheduled) return;
    pushScheduled = true;
    setImmediate(function () {
      pushScheduled = false;
      opts.emit('downloads', snapshot());
    });
  }

  // Finished entries are kept for the session only up to a fixed budget so
  // a long-lived session cannot grow this list without bound.
  function trimFinished() {
    var finishedIdx = [];
    items.forEach(function (d, i) {
      if (d.state !== 'progressing' && d.state !== 'paused') finishedIdx.push(i);
    });
    if (finishedIdx.length <= MAX_FINISHED) return;
    var drop = new Set(finishedIdx.slice(0, finishedIdx.length - MAX_FINISHED));
    items = items.filter(function (d, i) { return !drop.has(i); });
  }

  opts.session.on('will-download', function (event, item) {
    var dir = opts.downloadsDir() || app.getPath('downloads');
    var filename = sanitizeFilename(item.getFilename());
    var savePath = uniquePath(dir, filename);
    // Containment: the final path must stay inside the downloads directory.
    var resolvedDir = path.resolve(dir);
    var resolvedPath = path.resolve(savePath);
    if (resolvedPath !== resolvedDir && resolvedPath.indexOf(resolvedDir + path.sep) !== 0) {
      savePath = path.join(resolvedDir, filename);
    }
    try {
      fs.mkdirSync(dir, { recursive: true });
    } catch (e) { /* noop */ }
    item.setSavePath(savePath);

    var d = {
      id: nextId++,
      item: item,
      filename: path.basename(savePath),
      savePath: savePath,
      url: item.getURL() || '',
      state: 'progressing',
      received: 0,
      total: item.getTotalBytes() || 0,
      startTime: Date.now(),
      endTime: null
    };
    items.push(d);
    push();

    item.on('updated', function (e, state) {
      if (state === 'progressing' && typeof item.isPaused === 'function' && item.isPaused()) {
        state = 'paused';
      }
      d.state = state;
      d.received = item.getReceivedBytes();
      d.total = item.getTotalBytes() || d.total;
      push();
    });

    item.once('done', function (e, state) {
      d.state = state;
      d.received = item.getReceivedBytes();
      d.endTime = Date.now();
      d.item = null;
      trimFinished();
      push();
      if (state === 'completed' && opts.onCompleted) opts.onCompleted(d);
    });
  });

  function get(id) {
    return items.find(function (d) { return d.id === id; }) || null;
  }

  function pause(id) {
    var d = get(id);
    if (d && d.item && d.state === 'progressing' && d.item.canPause()) d.item.pause();
    push();
  }
  function resume(id) {
    var d = get(id);
    if (d && d.item && d.state === 'paused' && d.item.canResume()) d.item.resume();
    push();
  }
  function cancel(id) {
    var d = get(id);
    if (d && d.item && (d.state === 'progressing' || d.state === 'paused')) d.item.cancel();
    push();
  }
  function remove(id) {
    var d = get(id);
    if (d && d.item && (d.state === 'progressing' || d.state === 'paused')) {
      try { d.item.cancel(); } catch (e) { /* noop */ }
    }
    items = items.filter(function (entry) { return entry.id !== id; });
    push();
  }
  function clearFinished() {
    items = items.filter(function (d) {
      return d.state === 'progressing' || d.state === 'paused';
    });
    push();
  }

  function openFile(id) {
    var d = get(id);
    if (!d || d.state !== 'completed') return { ok: false, error: 'Download not completed' };
    if (!fs.existsSync(d.savePath)) return { ok: false, error: 'File no longer exists' };
    var ext = path.extname(d.savePath).toLowerCase();
    if (DANGEROUS_EXT.indexOf(ext) !== -1) {
      var choice = dialog.showMessageBoxSync({
        type: 'warning',
        buttons: ['Open', 'Cancel'],
        defaultId: 1,
        cancelId: 1,
        message: 'This file type can harm your computer.',
        detail: d.filename + ' is a ' + ext + ' file. Only open it if you trust the source.'
      });
      if (choice !== 0) return { ok: false, error: 'Cancelled' };
    }
    var err = shell.openPath(d.savePath);
    if (err) return { ok: false, error: String(err) };
    return { ok: true };
  }

  function showFolder(id) {
    var d = get(id);
    if (!d) return { ok: false };
    shell.showItemInFolder(d.savePath);
    return { ok: true };
  }

  function activeCount() {
    return items.filter(function (d) {
      return d.state === 'progressing' || d.state === 'paused';
    }).length;
  }

  return {
    list: snapshot,
    pause: pause,
    resume: resume,
    cancel: cancel,
    remove: remove,
    clearFinished: clearFinished,
    openFile: openFile,
    showFolder: showFolder,
    activeCount: activeCount
  };
}

module.exports = { createDownloads: createDownloads, uniquePath: uniquePath };
