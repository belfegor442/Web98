/*
 * Web98 shell: boot splash, desktop icons, start menu, taskbar clock,
 * explorer windows (My Computer / Recycle Bin), Run & Shut Down.
 */
(function () {
  'use strict';

  var desktop = document.getElementById('desktop');
  var startBtn = document.getElementById('startbtn');
  var startMenu = document.getElementById('startmenu');
  var clockEl = document.getElementById('clock');
  var offscreen = document.getElementById('offscreen');

  function div(parent, cls) {
    var n = document.createElement('div');
    if (cls) n.className = cls;
    if (parent) parent.appendChild(n);
    return n;
  }

  /* ================================ browser ============================== */
  function openBrowser() {
    return IE5.open({ id: 'ie5', maximized: true });
  }

  /* ============================ desktop icons ============================ */
  var icons = Array.prototype.slice.call(document.querySelectorAll('.dicon'));

  function clearIcons() {
    icons.forEach(function (i) { i.classList.remove('is-selected'); });
  }

  icons.forEach(function (ic) {
    ic.addEventListener('click', function (e) {
      e.stopPropagation();
      clearIcons();
      ic.classList.add('is-selected');
    });
    ic.addEventListener('dblclick', function () {
      activateIcon(ic.dataset.open);
    });
    ic.addEventListener('keydown', function (e) {
      if (e.key === 'Enter') activateIcon(ic.dataset.open);
    });
  });

  desktop.addEventListener('mousedown', function (e) {
    if (e.target === desktop || e.target.classList.contains('desktop__icons')) clearIcons();
  });

  function activateIcon(which) {
    if (which === 'my-computer') openMyComputer();
    else if (which === 'recycle-bin') openRecycleBin();
    else if (which === 'ie5') openBrowser();
  }

  /* =========================== explorer windows ========================== */
  function explorerWindow(cfg) {
    if (WM.list().has(cfg.id)) {
      WM.focus(cfg.id);
      return null;
    }
    return WM.open({
      id: cfg.id,
      title: cfg.title,
      icon: cfg.icon,
      width: cfg.width || 520,
      height: cfg.height || 380
    });
  }

  function statusStrip(parent, text, zone) {
    var sb = div(parent, 'ow-statusbar');
    var t = div(sb, 'ow-status-text');
    t.textContent = text;
    var z = div(sb, 'ow-zone');
    z.textContent = zone;
  }

  function selectOnly(list, item) {
    Array.prototype.forEach.call(list.children, function (c) {
      c.classList.toggle('is-selected', c === item);
    });
  }

  function openMyComputer() {
    var win = explorerWindow({
      id: 'my-computer',
      title: 'My Computer',
      icon: 'assets/icons/my-computer.svg',
      width: 540,
      height: 400
    });
    if (!win) return;

    var items = [
      {
        icon: 'floppy.svg',
        label: '3\u00bd Floppy (A:)',
        open: ['A:\\ is not accessible.', 'The device is not ready.'],
        dlg: 'warn'
      },
      {
        icon: 'hdd.svg',
        label: '(C:)',
        open: ['Local Disk (C:)', '2.01 GB total, 748 MB free of 2.01 GB.'],
        dlg: 'info'
      },
      {
        icon: 'cd.svg',
        label: '(D:)',
        open: ['There is no disc in the drive.', 'Insert a CD-ROM and try again.'],
        dlg: 'warn'
      },
      {
        icon: 'settings.svg',
        label: 'Control Panel',
        open: ['Control Panel is not available in Web98.', 'Use the Start menu instead.'],
        dlg: 'warn'
      },
      {
        icon: 'printer.svg',
        label: 'Printers',
        open: ['No printers are installed.', 'Install a printer to enable printing.'],
        dlg: 'warn'
      }
    ];

    var view = div(win.body, 'xview');
    var list = div(view, 'xview__list');

    items.forEach(function (it) {
      var d = div(list, 'xitem');
      var img = document.createElement('img');
      img.src = 'assets/icons/' + it.icon;
      img.alt = '';
      var span = document.createElement('span');
      span.textContent = it.label;
      d.appendChild(img);
      d.appendChild(span);
      d.addEventListener('click', function () { selectOnly(list, d); });
      d.addEventListener('dblclick', function () {
        WM.dialog({
          title: it.label,
          icon: it.dlg,
          heading: it.open[0],
          text: it.open.slice(1),
          width: 360
        });
      });
      list.appendChild(d);
    });

    statusStrip(win.body, items.length + ' object(s)', 'My Computer');
  }

  function openRecycleBin() {
    var win = explorerWindow({
      id: 'recycle-bin',
      title: 'Recycle Bin',
      icon: 'assets/icons/recycle-bin.svg',
      width: 500,
      height: 330
    });
    if (!win) return;

    var view = div(win.body, 'xview');
    var head = div(view, 'lhead');
    ['Name', 'Original Location', 'Date Deleted', 'Type', 'Size'].forEach(function (h, i) {
      var s = document.createElement('span');
      s.textContent = h;
      s.style.flex = i === 1 ? '1 1 auto' : '0 0 auto';
      s.style.minWidth = i === 0 ? '110px' : i === 4 ? '60px' : '0';
      head.appendChild(s);
    });
    var body = div(view, 'lbody');
    body.textContent = 'The Recycle Bin is empty.';

    statusStrip(win.body, '0 object(s)', 'Recycle Bin');
  }

  /* ============================== start menu ============================= */
  function toggleStart() {
    if (startMenu.hidden) openStart();
    else closeStart();
  }
  function openStart() {
    startMenu.hidden = false;
    startBtn.classList.add('is-open');
    startBtn.setAttribute('aria-expanded', 'true');
  }
  function closeStart() {
    startMenu.hidden = true;
    startBtn.classList.remove('is-open');
    startBtn.setAttribute('aria-expanded', 'false');
  }

  startBtn.addEventListener('click', function (e) {
    e.stopPropagation();
    toggleStart();
  });

  startMenu.addEventListener('click', function (e) {
    var item = e.target.closest('.sm-item');
    if (!item || item.classList.contains('is-disabled')) return;
    var action = item.dataset.action;
    if (!action) return;
    closeStart();
    startAction(action);
  });

  document.addEventListener('mousedown', function (e) {
    if (startMenu.hidden) return;
    if (e.target.closest('#startmenu') || e.target.closest('#startbtn')) return;
    closeStart();
  });

  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape' && !startMenu.hidden) closeStart();
  });

  function startAction(action) {
    switch (action) {
      case 'open-ie':
        openBrowser();
        break;
      case 'outlook':
        mailDialog();
        break;
      case 'help':
        WM.dialog({
          title: 'Web98 Help',
          icon: 'info',
          heading: 'Welcome to Web98',
          text: [
            'Web98 is a Windows 98 desktop that runs entirely in your browser. ' +
              'Double-click desktop icons, drag windows by their title bars, and ' +
              'explore the Start menu.',
            'Internet Explorer 5 browses a bundled 1999 web. Try View > Source, ' +
              'the Favorites button, or type an address that does not exist.',
            'Nothing is sent anywhere \u2014 it all works offline.'
          ],
          width: 420
        });
        break;
      case 'run':
        runDialog();
        break;
      case 'control-panel':
        WM.dialog({
          title: 'Control Panel',
          icon: 'warn',
          heading: 'Control Panel is not available in Web98.',
          text: ['Display, sound, and hardware settings are not simulated.'],
          width: 370
        });
        break;
      case 'printers':
        WM.dialog({
          title: 'Printers',
          icon: 'warn',
          heading: 'No printers are installed.',
          text: ['Install a printer to enable printing.'],
          width: 340
        });
        break;
      case 'find-files':
        WM.dialog({
          title: 'Find: All Files',
          icon: 'info',
          heading: 'Find is not available in Web98.',
          text: ['Use the SeekFinder page in Internet Explorer to search the Web.'],
          width: 380
        });
        break;
      case 'find-web':
        openBrowser().navigate('http://www.seekfinder.com/');
        break;
      case 'shutdown':
        shutdownDialog();
        break;
    }
  }

  function mailDialog() {
    WM.dialog({
      title: 'Outlook Express',
      icon: 'info',
      heading: 'Outlook Express is not installed',
      text: ['To use mail and news, ask your system administrator to install ' +
        'Outlook Express from the Windows 98 CD-ROM.'],
      width: 370
    });
  }

  /* ================================= Run ================================= */
  function runDialog() {
    WM.dialog({
      title: 'Run',
      width: 440,
      cancelValue: null,
      html:
        '<div class="dlg"><img class="dlg__icon" src="assets/icons/run.svg" alt="">' +
        '<div class="dlg__text">' +
        '<p>Type the name of a program, folder, document, or Internet resource, ' +
        'and Windows will open it for you.</p>' +
        '<div class="dlg__row"><b>Open:</b>' +
        '<input class="w98-input" data-run spellcheck="false"></div>' +
        '</div></div>',
      buttons: [
        {
          label: 'OK',
          value: function (root) {
            var i = root.querySelector('[data-run]');
            return i ? i.value.trim() : '';
          },
          def: true
        },
        { label: 'Cancel', value: null }
      ],
      onShow: function (root) {
        root.querySelector('[data-run]').focus();
      }
    }).then(runCommand);
  }

  function runCommand(cmd) {
    if (!cmd) return;

    if (/^iexplore(\.exe)?$/i.test(cmd)) {
      openBrowser();
      return;
    }
    if (/^(explorer|notepad|mspaint|winmine|sol|cmd|command|calc)(\.exe)?$/i.test(cmd)) {
      WM.dialog({
        title: 'Run',
        icon: 'error',
        heading: 'Cannot find the file',
        text: [
          "The file '" + cmd + "' (or one of its components) could not be found.",
          'Web98 ships with Internet Explorer only.'
        ],
        width: 390
      });
      return;
    }
    if (/^(www\.|[a-z0-9-]+(\.[a-z0-9-]+)+([\/?#]|$)|https?:\/\/|about:)/i.test(cmd)) {
      var b = openBrowser();
      b.navigate(cmd);
      return;
    }
    WM.dialog({
      title: 'Run',
      icon: 'error',
      heading: "Cannot find the file '" + cmd + "'",
      text: ['Make sure the file name and path are correct.'],
      width: 400
    });
  }

  /* =============================== Shut Down ============================= */
  function shutdownDialog() {
    WM.dialog({
      title: 'Shut Down Windows',
      width: 410,
      cancelValue: null,
      html:
        '<div class="dlg"><img class="dlg__icon" src="assets/icons/shutdown.svg" alt="">' +
        '<div class="dlg__text"><b>Shut Down Windows</b>' +
        '<p>What do you want the computer to do?</p>' +
        '<label class="radio"><input type="radio" name="sd" value="standby"> Stand by</label>' +
        '<label class="radio"><input type="radio" name="sd" value="shutdown" checked> Shut down</label>' +
        '<label class="radio"><input type="radio" name="sd" value="restart"> Restart</label>' +
        '</div></div>',
      buttons: [
        {
          label: 'OK',
          value: function (root) {
            var checked = root.querySelector('input[name="sd"]:checked');
            return checked ? checked.value : 'shutdown';
          },
          def: true
        },
        { label: 'Cancel', value: null }
      ]
    }).then(function (v) {
      if (v === 'shutdown') {
        showOffscreen(
          'It&rsquo;s now safe to turn off<br>your computer.',
          '(click anywhere to restart)'
        );
      } else if (v === 'restart') {
        location.reload();
      } else if (v === 'standby') {
        showOffscreen('&nbsp;', '(standby &mdash; click to wake the computer)');
      }
    });
  }

  function showOffscreen(text, hint) {
    offscreen.querySelector('.offscreens__text').innerHTML = text;
    offscreen.querySelector('.offscreens__hint').innerHTML = hint;
    offscreen.hidden = false;
  }

  offscreen.addEventListener('click', function () {
    location.reload();
  });

  /* ================================= clock =============================== */
  function tick() {
    var d = new Date();
    var h = d.getHours();
    var m = d.getMinutes();
    var ap = h >= 12 ? 'PM' : 'AM';
    h = h % 12;
    if (h === 0) h = 12;
    clockEl.textContent = h + ':' + (m < 10 ? '0' + m : m) + ' ' + ap;
  }
  tick();
  setInterval(tick, 10000);

  /* ================================= boot ================================ */
  (function boot() {
    var splash = document.getElementById('boot');
    if (!splash) return;
    var done = false;
    var timer = setTimeout(finish, 2300);
    splash.addEventListener('click', function () {
      clearTimeout(timer);
      finish();
    });

    function finish() {
      if (done) return;
      done = true;
      splash.classList.add('is-done');
      setTimeout(function () { splash.remove(); }, 450);
      openBrowser();
    }
  })();
})();
