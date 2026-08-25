/* portal_nav.js — Two-level nav, fragment loading, hash routing, health panel */
(function () {
  'use strict';

  var NAV_API = '/api/portal/nav';
  var SECTION_API = '/api/section/';
  var FETCH_TIMEOUT_MS = 10000;

  var navEl = document.getElementById('portal-nav');
  var contentEl = document.getElementById('content-pane');
  var hamburgerEl = document.getElementById('hamburger');
  var healthBadge = document.getElementById('health-badge');

  var categories = [];
  var itemMap = {};       // itemId → { cat, item }
  var currentItem = null;
  var activeFetchController = null;
  var fragmentLoadGeneration = 0;
  var backdropEl = null;

  // ---------- Utility ----------

  function escapeHtml(str) {
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(str));
    return div.innerHTML;
  }

  // ---------- Layout height calc ----------

  function updateLayoutHeight() {
    var header = document.querySelector('.portal-header');
    if (header) {
      document.documentElement.style.setProperty(
        '--portal-header-height-desktop',
        header.offsetHeight + 'px'
      );
    }
  }

  // ---------- Two-level nav build ----------

  function loadNavigationAssets(data) {
    var scripts = {};
    var styles = {};
    (data.categories || []).forEach(function (category) {
      (category.items || []).forEach(function (item) {
        if (item.portal_script) scripts[item.portal_script] = true;
        if (item.portal_style) styles[item.portal_style] = true;
      });
    });

    var loads = Object.keys(styles).map(function (path) {
      return new Promise(function (resolve, reject) {
        var style = document.createElement('link');
        style.rel = 'stylesheet';
        style.href = path;
        style.onload = resolve;
        style.onerror = function () { reject(new Error('Portal asset unavailable: ' + path)); };
        document.head.appendChild(style);
      });
    });
    loads = loads.concat(Object.keys(scripts).map(function (path) {
      return new Promise(function (resolve, reject) {
        var script = document.createElement('script');
        script.src = path;
        script.onload = resolve;
        script.onerror = function () { reject(new Error('Portal asset unavailable: ' + path)); };
        document.head.appendChild(script);
      });
    }));
    return Promise.all(loads);
  }

  function buildNav(cats) {
    categories = cats;
    itemMap = {};
    navEl.innerHTML = '';

    // Welcome link at top of nav
    var welcomeLink = document.createElement('a');
    welcomeLink.className = 'nav-welcome-link';
    welcomeLink.href = '#welcome';
    welcomeLink.innerHTML = '<span class="nav-welcome-icon">🏠</span> Home';
    welcomeLink.addEventListener('click', function (e) {
      e.preventDefault();
      navigateTo('welcome');
    });
    navEl.appendChild(welcomeLink);
    itemMap['welcome'] = { cat: null, item: { id: 'welcome', display_name: 'Home' } };

    cats.forEach(function (cat, catIdx) {
      // Category header
      var header = document.createElement('div');
      header.className = 'nav-category-header' + (catIdx === 0 ? ' expanded' : '');
      header.innerHTML =
        '<span class="nav-category-icon">' + (cat.icon || '') + '</span>' +
        '<span class="nav-category-label">' + escapeHtml(cat.display_name) + '</span>' +
        '<span class="nav-category-chevron"></span>';

      // Items container
      var itemsDiv = document.createElement('div');
      itemsDiv.className = 'nav-category-items' + (catIdx === 0 ? ' expanded' : '');

      cat.items.forEach(function (item) {
        itemMap[item.id] = { cat: cat, item: item };

        var link = document.createElement('a');
        link.className = 'nav-item';
        link.href = '#' + item.id;
        link.textContent = item.display_name;
        link.setAttribute('data-item', item.id);
        link.setAttribute('data-category', cat.id);
        link.addEventListener('click', function (e) {
          e.preventDefault();
          navigateTo(item.id);
        });
        itemsDiv.appendChild(link);
      });

      // Toggle expand/collapse
      header.addEventListener('click', function () {
        header.classList.toggle('expanded');
        itemsDiv.classList.toggle('expanded');
      });

      navEl.appendChild(header);
      navEl.appendChild(itemsDiv);
    });
  }

  // ---------- Active state ----------

  function setActive(itemId) {
    // Welcome link active state
    var welcomeLink = navEl.querySelector('.nav-welcome-link');
    if (welcomeLink) welcomeLink.classList.toggle('active', itemId === 'welcome');

    var links = navEl.querySelectorAll('.nav-item');
    for (var i = 0; i < links.length; i++) {
      links[i].classList.toggle('active', links[i].getAttribute('data-item') === itemId);
    }
  }

  function expandCategoryFor(itemId) {
    var info = itemMap[itemId];
    if (!info || !info.cat) return;
    var catId = info.cat.id;
    // Find and expand the matching category
    var headers = navEl.querySelectorAll('.nav-category-header');
    var items = navEl.querySelectorAll('.nav-category-items');
    for (var i = 0; i < headers.length; i++) {
      var label = headers[i].querySelector('.nav-category-label');
      if (label && label.textContent === info.cat.display_name) {
        headers[i].classList.add('expanded');
        items[i].classList.add('expanded');
        break;
      }
    }
  }

  // ---------- Fragment loading ----------

  function loadFragment(itemId) {
    if (currentItem === itemId) return;
    currentItem = itemId;
    var loadGeneration = ++fragmentLoadGeneration;
    setActive(itemId);
    expandCategoryFor(itemId);

    // Cancel any in-flight fetch
    if (activeFetchController) activeFetchController.abort();
    var fetchController = new AbortController();
    activeFetchController = fetchController;
    var signal = fetchController.signal;

    var timeoutId = setTimeout(function () {
      fetchController.abort();
    }, FETCH_TIMEOUT_MS);

    // Show loading
    contentEl.innerHTML =
      '<div class="fragment-loading">' +
      '<div class="spinner-border text-primary" role="status">' +
      '<span class="visually-hidden">Loading…</span></div>' +
      '<small class="text-muted">Loading section…</small></div>';

    // Remove any fragment-scoped CSS from previous fragment
    var oldLink = document.getElementById('fragment-css');
    if (oldLink) oldLink.remove();

    fetch(SECTION_API + itemId, { signal: signal })
      .then(function (res) {
        clearTimeout(timeoutId);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.text();
      })
      .then(function (html) {
        if (loadGeneration !== fragmentLoadGeneration) return;
        contentEl.innerHTML = html;
        // Convention-based fragment init
        var initFn = window['init_' + itemId.replace(/-/g, '_') + '_fragment'];
        return typeof initFn === 'function' ? Promise.resolve(initFn()) : null;
      })
      .then(function () {
        if (loadGeneration !== fragmentLoadGeneration) return;
        // Replace native <select> popups with themed custom dropdowns.
        // Runs after init so any selects added by initFn are also enhanced.
        // The MutationObserver inside dropdownEnhance keeps the UI in sync
        // when options are added later (e.g. by async fetches).
        if (typeof dropdownEnhanceAll === 'function') {
            dropdownEnhanceAll(contentEl);
        }
      })
      .catch(function (err) {
        if (loadGeneration !== fragmentLoadGeneration) return;
        clearTimeout(timeoutId);
        if (err.name === 'AbortError') {
          showError('Request timed out', itemId);
        } else {
          showError(err.message || 'Failed to load section', itemId);
        }
      });
  }

  function showError(message, itemId) {
    contentEl.innerHTML =
      '<div class="fragment-error">' +
      '<h3 class="text-danger">Error Loading Section</h3>' +
      '<p class="text-muted">' + escapeHtml(message) + '</p>' +
      '<button class="btn btn-primary btn-sm" id="retry-btn">Retry</button>' +
      '</div>';
    var retryBtn = document.getElementById('retry-btn');
    if (retryBtn) {
      retryBtn.addEventListener('click', function () {
        currentItem = null;
        loadFragment(itemId);
      });
    }
  }

  // ---------- Navigation ----------

  function navigateTo(itemId) {
    window.location.hash = '#' + itemId;
    loadFragment(itemId);
    closeNav();
  }

  function getItemFromHash() {
    var hash = window.location.hash.replace('#', '');
    return hash || null;
  }

  // ---------- Hamburger / mobile nav ----------

  function createBackdrop() {
    if (backdropEl) return;
    backdropEl = document.createElement('div');
    backdropEl.className = 'nav-backdrop';
    backdropEl.addEventListener('click', closeNav);
    document.body.appendChild(backdropEl);
  }

  function openNav() {
    navEl.classList.add('open');
    hamburgerEl.classList.add('active');
    createBackdrop();
    backdropEl.classList.add('visible');
  }

  function closeNav() {
    navEl.classList.remove('open');
    if (hamburgerEl) hamburgerEl.classList.remove('active');
    if (backdropEl) backdropEl.classList.remove('visible');
  }

  if (hamburgerEl) {
    hamburgerEl.addEventListener('click', function () {
      if (navEl.classList.contains('open')) {
        closeNav();
      } else {
        openNav();
      }
    });
  }

  // ---------- Hash change (browser back/forward) ----------

  window.addEventListener('hashchange', function () {
    var item = getItemFromHash();
    if (item && item !== currentItem) {
      loadFragment(item);
    }
  });

  // ---------- Health panel (managed by portal_health.js; only click-outside close here) ----------

  var healthExpanded = document.getElementById('health-expanded');

  document.addEventListener('click', function (e) {
    if (healthExpanded && healthExpanded.style.display !== 'none' &&
        !healthExpanded.contains(e.target) &&
        healthBadge && !healthBadge.contains(e.target)) {
      if (typeof toggleHealthWidget === 'function') toggleHealthWidget();
    }
  });

  // ---------- Theme toggle ----------

  var themeToggle = document.getElementById('theme-toggle');

  function getPreferredTheme() {
    var saved = localStorage.getItem('portal-theme');
    if (saved) return saved;
    return matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }

  function applyTheme(theme) {
    document.documentElement.setAttribute('data-bs-theme', theme);
  }

  if (themeToggle) {
    themeToggle.addEventListener('click', function () {
      var current = document.documentElement.getAttribute('data-bs-theme');
      var next = current === 'dark' ? 'light' : 'dark';
      applyTheme(next);
      localStorage.setItem('portal-theme', next);
    });
  }

  // ---------- Reboot toggle ----------

  var rebootToggle = document.getElementById('reboot-toggle');
  if (rebootToggle) {
    rebootToggle.addEventListener('click', function () {
      if (!confirm('Reboot the device now?')) return;
      if (typeof showRebootDialog === 'function') {
        showRebootDialog({
          title: 'Rebooting Device',
          message: 'Please wait while the device restarts\u2026',
          context: 'save'
        });
      }
      fetch(API_REBOOT, { method: 'POST' }).catch(function () {
        // TCP often drops mid-response as the device restarts; the reboot
        // dialog's reconnection poller handles the wait-and-redirect.
      });
    });
  }

  // Follow browser changes when no explicit preference saved
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function (e) {
    if (!localStorage.getItem('portal-theme')) {
      applyTheme(e.matches ? 'dark' : 'light');
    }
  });

  // ---------- Init ----------

  function init() {
    updateLayoutHeight();
    window.addEventListener('resize', updateLayoutHeight);

    fetch(NAV_API)
      .then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      })
      .then(function (data) {
        return loadNavigationAssets(data).then(function () { return data; });
      })
      .then(function (data) {
        // Expose primary category data for welcome hero card
        var primary = data.primary || null;
        window._portalPrimary = primary;

        buildNav(data.categories || []);

        // Startup fallback chain:
        // 1. In AP mode, primary fragment wins over any hash (the wizard is
        //    the only meaningful page; a stale #welcome from a prior session
        //    must not override it).
        // 2. Otherwise: URL hash if present and exists in visible nav
        // 3. Primary fragment if defined and exists in visible nav
        // 4. #welcome
        var itemId = null;
        if (data.ap_mode && primary && primary.fragment && itemMap[primary.fragment]) {
          itemId = primary.fragment;
        }
        if (!itemId) {
          itemId = getItemFromHash();
          if (itemId && !itemMap[itemId]) itemId = null;
        }
        if (!itemId && primary && primary.fragment && itemMap[primary.fragment]) {
          itemId = primary.fragment;
        }
        if (!itemId) itemId = 'welcome';
        navigateTo(itemId);

        // Populate header badges + start health polling once per page load.
        // These functions live in portal_config.js / portal_health.js
        // (always-loaded chunk) and were previously only invoked from
        // pad-editor / sensor-data fragments — meaning headless or AP-mode
        // landings left the badges at their placeholder values forever.
        if (typeof loadVersion === 'function') loadVersion();
        if (typeof initHealthWidget === 'function') initHealthWidget();
      })
      .catch(function (err) {
        navEl.innerHTML =
          '<p class="text-danger p-3" style="font-size:0.85rem;">' +
          'Failed to load navigation: ' + escapeHtml(err.message) + '</p>';
      });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
