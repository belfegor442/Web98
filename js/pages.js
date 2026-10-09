/*
 * Bundled "1999" web pages served by the Web98 browser.
 * Everything is plain HTML strings — no fetch(), so it works from file://.
 */
var Pages = (function () {
  'use strict';

  /* Known hosts → path → route. Unknown hosts get the DNS error page. */
  var HOSTS = {
    'www.web98.com': { paths: { '/': 'home', '/index.html': 'home' } },
    'www.seekfinder.com': { paths: { '/': 'search', '/index.html': 'search' } },
    'members.geocities.com': {
      paths: {
        '/siliconvalley/4040/': 'fan',
        '/siliconvalley/4040/index.html': 'fan'
      }
    },
    'www.webdaily.com': { paths: { '/': 'news', '/index.html': 'news' } },
    'www.bargainbytes.com': { paths: { '/': 'shop', '/index.html': 'shop' } }
  };

  var HOME_URL = 'http://www.web98.com/';

  /* Index used by the search engine, favorites and the address dropdown. */
  var SITES = [
    {
      url: HOME_URL,
      title: 'Web98 \u2014 Start Page',
      desc: 'Directory of the best the World Wide Web has to offer. Updated daily since 1997.',
      kw: 'web98 start page home directory portal gateway best web'
    },
    {
      url: 'http://www.seekfinder.com/',
      title: 'SeekFinder \u2014 Search the Web',
      desc: 'Find what you are looking for on the Internet. Indexing 4,206,199 pages.',
      kw: 'search engine find seekfinder query info lookup google altavista'
    },
    {
      url: 'http://members.geocities.com/SiliconValley/4040/',
      title: "Dave's Home Page \u2014 Dave's Web Corner",
      desc: 'Cool links, my cat picture, Java applets and a guestbook. Sign it!',
      kw: 'dave home page geocities cool links cat java guestbook fun personal'
    },
    {
      url: 'http://www.webdaily.com/',
      title: 'The Web Daily \u2014 News from the Net',
      desc: 'Breaking technology news, every day at 6pm. Y2K coverage, browser wars, IPOs.',
      kw: 'news headlines daily technology y2k apple ebay napster browser netscape microsoft'
    },
    {
      url: 'http://www.bargainbytes.com/',
      title: 'BargainBytes \u2014 Discount Computer Hardware',
      desc: 'Cheap PC parts by mail order. 56K modems, Voodoo cards, RAM and monitors.',
      kw: 'shop store buy parts computer hardware cheap modem voodoo monitor ram memory sale'
    }
  ];

  var ARTICLES = [
    {
      id: '1',
      title: 'Y2K Countdown: Is Your Toaster Ready?',
      byline: 'By Janet Marek, Web Daily Staff',
      date: 'March 17, 1999',
      body: [
        'WASHINGTON \u2014 With less than ten months remaining before the year 2000 problem is scheduled to strike, government auditors warned Tuesday that thousands of embedded systems \u2014 from elevator controllers to microwave ovens \u2014 may still be running two-digit date logic.',
        '"A microwave that thinks it is 1999 will not explode," said Dr. Alan Prager of the National Institute for Standards. "It will simply refuse to cook, because it believes the warranty has expired."',
        'Analysts estimate that consumers will spend $18 billion on remediation this year, spawning a booming cottage industry of consultants who will, in many cases, simply unplug the device and plug it back in.',
        'The President is expected to sign emergency legislation next week ordering all vending machines to display the correct year by December 31.'
      ]
    },
    {
      id: '2',
      title: 'Apple Ships New iMac; Analysts Split on Translucent Future',
      byline: 'By Chris Voss, Web Daily Staff',
      date: 'March 16, 1999',
      body: [
        'CUPERTINO \u2014 Apple Computer began shipping its Bondi Blue iMac to dealers this week, and early reviews praise the machine\u2019s striking translucent case while questioning the absence of a floppy drive.',
        '"Who needs a floppy when you have the Internet?" said Apple CEO Steve Jobs at the announcement. Critics note that the Internet, in this case, requires a $29.95 per month subscription.',
        'The iMac ships with a 233MHz G3 processor, 32MB of RAM, and a 4GB hard drive for $1,299. Analysts at Dataquest predict the machine will sell well to education markets and to anyone who has ever wanted a computer that matches their kitchen appliances.'
      ]
    },
    {
      id: '3',
      title: 'PEZ Dispenser Sells for $28,000 on Online Auction',
      byline: 'By Dolores Fund, Web Daily Staff',
      date: 'March 15, 1999',
      body: [
        'SAN JOSE \u2014 A rare 1959 Pink Panther PEZ dispenser sold for $28,000 Tuesday on an online auction site, shattering the previous record for candy-vending memorabilia.',
        'The winning bidder, an anonymous collector from Ohio, said he plans to keep the dispenser "right next to the other 4,000," and denied that he had overpaid.',
        'eBay officials said the sale proves that "any object, given a sufficiently aerodynamic supply of nostalgia, will eventually find its price." The site now lists more than two million items for sale at any given moment.'
      ]
    },
    {
      id: '4',
      title: 'Browser War II: Netscape and Microsoft Trade Blows',
      byline: 'By Roy Halston, Web Daily Staff',
      date: 'March 12, 1999',
      body: [
        'The battle for the desktop continued this week as Netscape Communications and Microsoft both pledged to ship major browser updates before the holiday season.',
        'Netscape promises "a faster engine and better mail," while Microsoft counters with tighter integration into the operating system and a dancing paperclip.',
        'Webmasters report that the conflict has left them maintaining three versions of every page: one for Netscape, one for Internet Explorer, and one for the 4% of users still browsing with Lynx on a terminal at the university library.'
      ]
    }
  ];

  var SHOP_ITEMS = [
    { sku: 'VV-3000', name: '3dfx Voodoo3 3000 AGP', price: '$179.00', note: '16MB, ships in 3-5 days' },
    { sku: 'SB-LIVE', name: 'Sound Blaster Live! Value', price: '$99.00', note: '64-voice, DOS compatible' },
    { sku: 'MTR-15C', name: '15" SVGA Monitor (Trinitron)', price: '$249.00', note: '0.28mm dot pitch' },
    { sku: 'MDM-56K', name: '56K V.90 Fax Modem', price: '$49.00', note: 'Internal, free shipping' },
    { sku: 'ZIP-100', name: 'Iomega ZIP 100 Drive', price: '$89.00', note: 'Parallel port, 100 disks extra' },
    { sku: 'RAM-064', name: '64MB PC100 SDRAM', price: '$74.00', note: 'One stick, no questions asked' },
    { sku: 'CDR-006', name: '6x CD-ROM Burner Kit', price: '$199.00', note: 'Includes 10 CDR discs' },
    { sku: 'HUB-005', name: '5-port 10BASE-T Hub', price: '$59.00', note: 'For your home network' }
  ];

  /* ------------------------------- helpers -------------------------------- */
  function L(url, text) {
    return '<a href="' + url + '">' + text + '</a>';
  }

  function esc(s) {
    return String(s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  }

  function counter(digs) {
    var out = '';
    for (var i = 0; i < digs.length; i++) out += '<i>' + digs[i] + '</i>';
    return '<span class="ctr">' + out + '</span>';
  }

  /* -------------------------------- pages --------------------------------- */
  function home() {
    return (
      '<div class="pg pg-home">' +
        '<table class="home-hdr" width="100%" cellpadding="0" cellspacing="0"><tr>' +
          '<td class="home-logo">Web<b>98</b></td>' +
          '<td class="home-slogan" align="right">The on-ramp to the Information Superhighway</td>' +
        '</tr></table>' +
        '<p class="home-date">Thursday, March 18, 1999</p>' +
        '<div class="rainbow"></div>' +
        '<form class="home-search" data-search>' +
          '<b>Seek the Web:</b> ' +
          '<input class="q" type="text" name="q" size="30" maxlength="80" value=""> ' +
          '<button type="submit" class="home-go">Seek!</button> ' +
          '<span class="home-note">(powered by SeekFinder)</span>' +
        '</form>' +
        '<div class="home-cols">' +
          '<div class="home-col">' +
            '<div class="box-h">Cool Places</div>' +
            '<ul>' +
              '<li>' + L('http://www.seekfinder.com/', 'SeekFinder') + ' \u2014 find anything on the Web</li>' +
              '<li>' + L('http://members.geocities.com/SiliconValley/4040/', "Dave's Home Page") + ' \u2014 cool links &amp; a guestbook</li>' +
              '<li>' + L('http://www.webdaily.com/', 'The Web Daily') + ' \u2014 technology news</li>' +
            '</ul>' +
          '</div>' +
          '<div class="home-col">' +
            '<div class="box-h">Shopping</div>' +
            '<ul>' +
              '<li>' + L('http://www.bargainbytes.com/', 'BargainBytes') + ' \u2014 discount PC parts</li>' +
              '<li>' + L('http://www.somethingelse.com/', 'SomethingElse.com') + ' \u2014 (site moved?)</li>' +
              '<li>' + L('http://www.yahoo.com/', 'Yahoo!') + ' \u2014 directory</li>' +
            '</ul>' +
          '</div>' +
        '</div>' +
        '<div class="uc"><span>Under construction since 1997</span></div>' +
        '<hr>' +
        '<p class="home-foot">' +
          'You are visitor ' + counter('0013374') + ' since January 1997<br>' +
          'Best viewed at 800&times;600 with Internet Explorer 5<br>' +
          'Copyright &copy; 1999 Web98 Online \u2014 ' +
          L(HOME_URL, 'Home') + ' | <a href="mailto:webmaster@web98.com">Webmaster</a> | ' +
          '<a href="http://www.web98.com/guestbook.html">Guestbook</a>' +
        '</p>' +
      '</div>'
    );
  }

  function search(params) {
    var q = (params && params.q) || '';
    var body;

    if (!q) {
      body =
        '<p class="res-sub">Enter one or more words to search the Web index.</p>' +
        '<p class="res-sub">Hot searches today: <b>disco</b> &middot; <b>y2k</b> &middot; ' +
        '<b>free screensavers</b> &middot; <b>mp3</b></p>';
    } else {
      var tokens = q.toLowerCase().split(/\s+/).filter(Boolean);
      var hits = SITES.filter(function (s) {
        var hay = (s.title + ' ' + s.desc + ' ' + s.kw).toLowerCase();
        return tokens.every(function (t) { return hay.indexOf(t) !== -1; });
      });
      if (!hits.length) {
        body =
          '<p class="res-sub"><b>No pages found</b> for &ldquo;' + esc(q) + '&rdquo;.</p>' +
          '<p class="res-sub">SeekFinder has indexed 4,206,199 pages, but it is 1999 and your ' +
          'search words may be too modern. Try: <i>dave</i>, <i>news</i>, <i>cheap</i>, <i>search</i>.</p>';
      } else {
        body = '<p class="res-sub">Found <b>' + hits.length + '</b> page' +
          (hits.length === 1 ? '' : 's') + ' (0.13 seconds)</p>';
        hits.forEach(function (s, i) {
          body +=
            '<div class="res">' +
              '<div class="res-t">' + (i + 1) + '. ' + L(s.url, esc(s.title)) + '</div>' +
              '<div class="res-u">' + s.url + '</div>' +
              '<div class="res-d">' + esc(s.desc) + '</div>' +
            '</div>';
        });
      }
    }

    return (
      '<div class="pg pg-search">' +
        '<div class="sf-hdr">' +
          '<span class="sf-logo">Seek<b>F</b><i>i</i><em>nder</em></span>' +
          '<span class="sf-tag">seek &amp; you shall find</span>' +
        '</div>' +
        '<form class="sf-form" data-search>' +
          '<input class="q" type="text" name="q" size="34" maxlength="80" value="' + esc(q) + '"> ' +
          '<button type="submit">Seek It!</button>' +
        '</form>' +
        body +
        '<hr>' +
        '<p class="res-foot">' + L(HOME_URL, 'Web98 Home') + ' &middot; ' +
          'SeekFinder &copy; 1999 \u2014 not affiliated with any real search engine</p>' +
      '</div>'
    );
  }

  function fan() {
    return (
      '<div class="pg pg-fan">' +
        '<marquee scrollamount="5" class="fan-marquee">*** Welcome to DAVE\u2019S HOME PAGE!!! *** This page is best viewed at 800x600 *** Please sign my guestbook before you leave ***</marquee>' +
        '<h1>Dave\u2019s Web Corner</h1>' +
        '<p class="fan-sub">~ the coolest place on the World Wide Web ~</p>' +
        '<div class="fan-photo"><svg viewBox="0 0 160 120" width="160" height="120">' +
          '<rect width="160" height="120" fill="#7ec8e3"/>' +
          '<ellipse cx="80" cy="126" rx="66" ry="34" fill="#5aa9d6"/>' +
          '<path d="M44 46 L52 14 L76 36 Z" fill="#f0a030" stroke="#b06a00" stroke-width="2"/>' +
          '<path d="M116 46 L108 14 L84 36 Z" fill="#f0a030" stroke="#b06a00" stroke-width="2"/>' +
          '<ellipse cx="80" cy="68" rx="44" ry="37" fill="#f0a030" stroke="#b06a00" stroke-width="2"/>' +
          '<path d="M64 38 q6 8 0 15 M80 34 q6 8 0 15 M96 38 q-6 8 0 15" stroke="#c07000" fill="none" stroke-width="3"/>' +
          '<ellipse cx="65" cy="63" rx="7" ry="9" fill="#fff" stroke="#000" stroke-width="1.5"/>' +
          '<ellipse cx="95" cy="63" rx="7" ry="9" fill="#fff" stroke="#000" stroke-width="1.5"/>' +
          '<circle cx="65" cy="64" r="3.4" fill="#1a7a1a"/>' +
          '<circle cx="95" cy="64" r="3.4" fill="#1a7a1a"/>' +
          '<path d="M76 77 L84 77 L80 82 Z" fill="#e06080" stroke="#a03050" stroke-width="1"/>' +
          '<path d="M80 82 q-6 8 -12 2 M80 82 q6 8 12 2" stroke="#b06a00" fill="none" stroke-width="2"/>' +
          '<path d="M42 71 H22 M42 78 H25 M118 71 H138 M118 78 H135" stroke="#333" stroke-width="1.5"/>' +
        '</svg><div class="fan-cap">Mr. Whiskers, enjoying the modem</div></div>' +
        '<p>Hi! I\u2019m Dave. Welcome to my little corner of the Internet. I made this page myself in Notepad, ' +
        'and I update it <b>whenever Mom lets me use the computer</b>. Last updated: March 18, 1999.</p>' +
        '<div class="uc"><span>Under construction</span></div>' +
        '<h2>My Cool Links</h2>' +
        '<ul>' +
          '<li>' + L('http://www.web98.com/', 'Web98 Start Page') + ' \u2014 where I start every day</li>' +
          '<li>' + L('http://www.webdaily.com/', 'The Web Daily') + ' \u2014 I read the news on here</li>' +
          '<li>' + L('http://www.bargainbytes.com/', 'BargainBytes') + ' \u2014 saving up for a Voodoo3</li>' +
          '<li>' + L('http://www.angelfire.com/', 'Angelfire') + ' \u2014 free home pages!!</li>' +
        '</ul>' +
        '<h2>Guestbook</h2>' +
        '<p><button type="button" class="fan-btn" data-guestbook>Sign my guestbook!</button> ' +
        '(' + counter('0004217') + ' entries so far)</p>' +
        '<h2>WebRing</h2>' +
        '<p class="ring">' +
          L('http://members.geocities.com/SiliconValley/4040/prev.html', '&lt;&lt; Prev') + ' | ' +
          L('http://www.webring.org/', 'Ring Home') + ' | ' +
          L('http://members.geocities.com/SiliconValley/4040/next.html', 'Next &gt;&gt;') +
          '<br><span class="ring-n">This site is part of the <b>GeoCities Personal Page WebRing</b></span>' +
        '</p>' +
        '<hr>' +
        '<p class="fan-foot">' +
          'This page hosted by GeoCities \u2014 get your own <b>FREE</b> home page!<br>' +
          'E-mail me: dave@geocities.com &middot; ' + L(HOME_URL, 'Back to Web98') +
        '</p>' +
      '</div>'
    );
  }

  function news(params) {
    var id = params && params.id;
    if (id) {
      var a = null;
      for (var i = 0; i < ARTICLES.length; i++) if (ARTICLES[i].id === id) a = ARTICLES[i];
      if (a) {
        var out =
          '<div class="pg pg-news">' +
            '<div class="news-masthead"><b>The Web Daily</b><span>Technology news for the connected</span></div>' +
            '<div class="news-rule"></div>' +
            '<h1 class="art-h">' + esc(a.title) + '</h1>' +
            '<p class="art-by">' + esc(a.byline) + ' &middot; ' + esc(a.date) + '</p>';
        a.body.forEach(function (p) { out += '<p>' + esc(p) + '</p>'; });
        out +=
            '<hr>' +
            '<p>' + L('http://www.webdaily.com/', '&laquo; Return to the front page') + '</p>' +
          '</div>';
        return out;
      }
    }

    var list = '';
    ARTICLES.forEach(function (a) {
      list +=
        '<li>' +
        L('http://www.webdaily.com/?id=' + a.id, esc(a.title)) +
        ' <span class="news-dek">' + esc(a.date) + '</span></li>';
    });

    return (
      '<div class="pg pg-news">' +
        '<div class="news-masthead"><b>The Web Daily</b><span>Technology news for the connected</span></div>' +
        '<div class="news-rule"></div>' +
        '<p class="news-date">Thursday, March 18, 1999 &mdash; Late Edition</p>' +
        '<div class="news-cols">' +
          '<div class="news-main">' +
            '<b class="box-h">Top Stories</b>' +
            '<ol class="news-list">' + list + '</ol>' +
          '</div>' +
          '<div class="news-side">' +
            '<b class="box-h">Market</b>' +
            '<table class="news-ticker" cellpadding="2" cellspacing="0">' +
              '<tr><td>NASDAQ</td><td class="up">2,474.12 +41.08</td></tr>' +
              '<tr><td>MSFT</td><td class="up">$92.63 +1.44</td></tr>' +
              '<tr><td>AAPL</td><td class="down">$34.75 -0.31</td></tr>' +
              '<tr><td>NSCP</td><td class="down">$28.12 -1.06</td></tr>' +
            '</table>' +
            '<b class="box-h">Weather</b>' +
            '<p class="news-weather">Sunny, 64&deg;F<br><span>Perfect modem weather</span></p>' +
            '<b class="box-h">Poll</b>' +
            '<p class="news-poll">Which browser will win?<br>' +
              '<span>IE 5 &nbsp;64%</span><br><span>Netscape &nbsp;31%</span><br><span>Lynx &nbsp;5%</span></p>' +
          '</div>' +
        '</div>' +
        '<hr>' +
        '<p class="news-foot">' + L(HOME_URL, 'Web98 Home') + ' &middot; ' +
          'Copyright &copy; 1999 The Web Daily</p>' +
      '</div>'
    );
  }

  function shop() {
    var rows = '';
    SHOP_ITEMS.forEach(function (it) {
      rows +=
        '<tr>' +
          '<td class="sh-sku">' + it.sku + '</td>' +
          '<td class="sh-name">' + esc(it.name) + '<div class="sh-note">' + esc(it.note) + '</div></td>' +
          '<td class="sh-price">' + it.price + '</td>' +
          '<td class="sh-buy"><button type="button" data-buy="' + esc(it.name) + '">Buy Now</button></td>' +
        '</tr>';
    });

    return (
      '<div class="pg pg-shop">' +
        '<table class="sh-hdr" width="100%" cellpadding="0" cellspacing="0"><tr>' +
          '<td class="sh-logo">Bargain<b>Bytes</b></td>' +
          '<td class="sh-phone" align="right">Order by phone:<br><b>1-800-BYTES-99</b></td>' +
        '</tr></table>' +
        '<p class="sh-tag">Discount computer hardware by mail order \u2014 we accept checks &amp; money orders</p>' +
        '<div class="sh-shipping">&#9733; FREE SHIPPING on orders over $100 &#9733;</div>' +
        '<table class="sh-table" cellpadding="4" cellspacing="0">' +
          '<tr class="sh-head"><td>SKU</td><td>Item</td><td>Price</td><td>&nbsp;</td></tr>' +
          rows +
        '</table>' +
        '<p class="sh-legal">All prices in U.S. dollars. Allow 6\u20138 weeks for delivery. ' +
        'BargainBytes is not responsible for items that arrive DOA, on fire, or in a different decade.</p>' +
        '<hr>' +
        '<p class="sh-foot">' + L(HOME_URL, 'Web98 Home') + ' &middot; ' +
          L('http://www.webdaily.com/', 'Tech News') + '</p>' +
      '</div>'
    );
  }

  function blank() {
    return '<div class="pg pg-blank"></div>';
  }

  function errorPage(kind, url) {
    var isDns = kind === 'dns';
    var heading = isDns ? 'Cannot find server' : 'The page cannot be displayed';
    var detail = isDns
      ? 'The server <b>' + esc(url) + '</b> could not be found. The Web site might be experiencing ' +
        'technical difficulties, or your modem may have unplugged itself again.'
      : 'HTTP 404 \u2014 File not found. The page you are looking for is currently unavailable. ' +
        'The Web site might be experiencing technical difficulties, or you may need to adjust your ' +
        'browser settings.';
    var extra = isDns ? '' :
      '<li>Make sure the page address <b>' + esc(url) + '</b> is spelled correctly.</li>';

    return (
      '<div class="pg pg-err">' +
        '<h1>' + heading + '</h1>' +
        '<p class="err-ie">Internet Explorer</p>' +
        '<p>' + detail + '</p>' +
        '<hr>' +
        '<p><b>Please try the following:</b></p>' +
        '<ul>' +
          '<li>Click the <b>Refresh</b> button, or try again later.</li>' +
          extra +
          '<li>If you would like to search the Web, visit ' +
            L('http://www.seekfinder.com/', 'SeekFinder') + '.</li>' +
          '<li>Go back to the ' + L(HOME_URL, 'Web98 start page') + ' and pick a link from the directory.</li>' +
        '</ul>' +
        '<hr>' +
        '<p class="err-sig">' + (isDns ? 'Cannot find server or DNS Error' : 'HTTP 404 \u2014 File not found') +
          '<br>Internet Explorer</p>' +
      '</div>'
    );
  }

  /* -------------------------------- routing -------------------------------- */
  function resolve(url) {
    if (!url) return null;
    if (/^about:/i.test(url)) {
      if (/^about:blank$/i.test(url)) return { route: 'blank', params: {}, url: 'about:blank' };
      return { route: 'dns', params: {}, url: url };
    }
    var u;
    try {
      u = new URL(url);
    } catch (e) {
      return null;
    }
    if (u.protocol !== 'http:' && u.protocol !== 'https:') {
      return { route: 'dns', params: {}, url: url };
    }
    var host = HOSTS[u.hostname.toLowerCase()];
    var path = u.pathname.toLowerCase();
    var params = {};
    u.searchParams.forEach(function (v, k) { params[k] = v; });

    if (!host) return { route: 'dns', params: params, url: url };
    var route = host.paths[path];
    if (!route && path === '/') route = host.paths['/'];
    if (!route) return { route: 'error404', params: params, url: url };
    return { route: route, params: params, url: u.href };
  }

  function meta(route, params) {
    switch (route) {
      case 'home': return { title: 'Web98 \u2014 Start Page' };
      case 'search': return { title: 'SeekFinder \u2014 Search the Web' };
      case 'fan': return { title: "Dave's Home Page \u2014 Dave's Web Corner" };
      case 'news': {
        if (params && params.id) {
          for (var i = 0; i < ARTICLES.length; i++) {
            if (ARTICLES[i].id === params.id) {
              return { title: ARTICLES[i].title + ' \u2014 The Web Daily' };
            }
          }
        }
        return { title: 'The Web Daily \u2014 News from the Net' };
      }
      case 'shop': return { title: 'BargainBytes \u2014 Discount Computer Hardware' };
      case 'blank': return { title: 'about:blank' };
      case 'error404': return { title: 'The page cannot be displayed' };
      case 'dns': return { title: 'Cannot find server' };
      default: return { title: 'Internet Explorer' };
    }
  }

  function render(route, params) {
    switch (route) {
      case 'home': return home();
      case 'search': return search(params || {});
      case 'fan': return fan();
      case 'news': return news(params || {});
      case 'shop': return shop();
      case 'blank': return blank();
      case 'error404': return errorPage('404', (params && params.url) || '');
      case 'dns': return errorPage('dns', (params && params.url) || '');
      default: return errorPage('dns', '');
    }
  }

  return {
    SITES: SITES,
    HOME_URL: HOME_URL,
    resolve: resolve,
    meta: meta,
    render: render
  };
})();
