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
  var navigationReady = false;
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

  var navigationAssetLoads = {};
  function loadItemAssets(item) {
    function loadNavigationAsset(path, kind) {
      if (navigationAssetLoads[path]) return navigationAssetLoads[path];
      navigationAssetLoads[path] = new Promise(function (resolve, reject) {
        var asset = document.createElement(kind === 'style' ? 'link' : 'script');
        if (kind === 'style') {
          asset.rel = 'stylesheet';
          asset.href = path;
        } else {
          asset.src = path;
        }
        asset.onload = resolve;
        asset.onerror = function () {
          asset.remove();
          delete navigationAssetLoads[path];
          reject(new Error('Portal asset unavailable: ' + path));
        };
        document.head.appendChild(asset);
      });
      return navigationAssetLoads[path];
    }

    var loads = [];
    if (item && item.portal_style) loads.push(loadNavigationAsset(item.portal_style, 'style'));
    if (item && item.portal_script) loads.push(loadNavigationAsset(item.portal_script, 'script'));
    return Promise.all(loads);
  }

  window.portalLoadItemAssets = function (itemId) {
    return loadItemAssets(itemMap[itemId] && itemMap[itemId].item);
  };

  function buildNav(cats) {
    categories = cats;
    itemMap = {};
    navEl.innerHTML = '';

    // Welcome link at top of nav
    var welcomeLink = document.createElement('a');
    welcomeLink.className = 'nav-welcome-link';
    welcomeLink.href = '#welcome';
    welcomeLink.innerHTML = '<span class="portal-icon" aria-hidden="true">home</span> Home';
    welcomeLink.addEventListener('click', function (e) {
      e.preventDefault();
      navigateTo('welcome');
    });
    navEl.appendChild(welcomeLink);
    itemMap['welcome'] = { cat: null, item: { id: 'welcome', display_name: 'Home' } };

    cats.forEach(function (cat, index) {
      // Category header
      var header = document.createElement('div');
      header.className = 'nav-category-header';
      header.setAttribute('data-category', cat.id);
      var categoryLink = document.createElement('a');
      categoryLink.className = 'nav-category-link';
      categoryLink.href = '#' + cat.items[0].id;
      categoryLink.innerHTML =
        portalCategoryIcon(cat.id, cat.icon) +
        '<span class="nav-category-label">' + escapeHtml(cat.display_name) + '</span>';
      categoryLink.addEventListener('click', function (e) {
        e.preventDefault();
        navigateTo(cat.items[0].id);
      });

      var toggle = document.createElement('button');
      toggle.type = 'button';
      toggle.className = 'nav-category-toggle';
      toggle.setAttribute('aria-controls', 'nav-category-items-' + index);
      toggle.innerHTML = '<span class="nav-category-chevron" aria-hidden="true"></span>';
      toggle.addEventListener('click', function () {
        toggleExpandedCategory(header);
      });
      header.appendChild(categoryLink);
      header.appendChild(toggle);

      // Items container
      var itemsDiv = document.createElement('div');
      itemsDiv.className = 'nav-category-items';
      itemsDiv.id = 'nav-category-items-' + index;

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

      navEl.appendChild(header);
      navEl.appendChild(itemsDiv);
    });
    setExpandedCategory(null);
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
    var info = itemMap[itemId];
    var headers = navEl.querySelectorAll('.nav-category-header');
    for (var j = 0; j < headers.length; j++) {
      headers[j].classList.toggle('active', !!info && !!info.cat &&
        headers[j].getAttribute('data-category') === info.cat.id);
    }
  }

  function setCategoryExpanded(header, items, expanded) {
    header.classList.toggle('expanded', expanded);
    items.classList.toggle('expanded', expanded);
    var toggle = header.querySelector('.nav-category-toggle');
    toggle.setAttribute('aria-expanded', String(expanded));
    toggle.setAttribute('aria-label',
      (expanded ? 'Collapse ' : 'Expand ') + header.querySelector('.nav-category-label').textContent);
  }

  function setExpandedCategory(catId) {
    var headers = navEl.querySelectorAll('.nav-category-header');
    var items = navEl.querySelectorAll('.nav-category-items');
    for (var i = 0; i < headers.length; i++) {
      setCategoryExpanded(headers[i], items[i], headers[i].getAttribute('data-category') === catId);
    }
  }

  function toggleExpandedCategory(target) {
    var headers = navEl.querySelectorAll('.nav-category-header');
    var items = navEl.querySelectorAll('.nav-category-items');
    var info = itemMap[currentItem];
    var activeCatId = info && info.cat ? info.cat.id : null;
    var catId = target.getAttribute('data-category');
    var opening = !target.classList.contains('expanded');
    for (var i = 0; i < headers.length; i++) {
      var id = headers[i].getAttribute('data-category');
      var expanded = headers[i].classList.contains('expanded');
      if (id === catId) expanded = opening;
      else if (opening && catId !== activeCatId && id !== activeCatId) expanded = false;
      setCategoryExpanded(headers[i], items[i], expanded);
    }
  }

  function expandCategoryFor(itemId) {
    var info = itemMap[itemId];
    setExpandedCategory(info && info.cat ? info.cat.id : null);
  }

  // ---------- Fragment loading ----------

  async function loadFragment(itemId, revealCategory) {
    if (typeof portalOperation !== 'undefined' && portalOperation.active) {
      history.replaceState(null, '', '#' + currentItem);
      return false;
    }
    if (currentItem === itemId) {
      if (revealCategory !== false) expandCategoryFor(itemId);
      return;
    }
    if (currentItem === 'pad-editor' && typeof padWorkspace !== 'undefined' && padWorkspace) {
      if (padDirty && !await padWorkspaceConfirm('Discard pad edits and leave the editor?')) {
        history.replaceState(null, '', '#' + currentItem);
        return false;
      }
      padWorkspaceDispose();
    }
    window.dispatchEvent(new CustomEvent('portal-fragment-leave', { detail: currentItem }));
    currentItem = itemId;
    var loadGeneration = ++fragmentLoadGeneration;
    setActive(itemId);
    if (revealCategory !== false) expandCategoryFor(itemId);

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
      .then(async function (html) {
        await loadItemAssets(itemMap[itemId] && itemMap[itemId].item);
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
    return true;
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

  async function navigateTo(itemId, revealCategory) {
    if (await loadFragment(itemId, revealCategory) !== false) {
      window.location.hash = '#' + itemId;
      closeNav();
    }
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
    if (!navigationReady) return;
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
    portalEnsureMaterialSymbols();
    updateLayoutHeight();
    window.addEventListener('resize', updateLayoutHeight);

    fetch(NAV_API)
      .then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      })
      .then(function (data) {
        // Expose primary category data for welcome hero card
        var primary = data.primary || null;
        window._portalPrimary = primary;

        buildNav(data.categories || []);
        navigationReady = true;

        // Startup fallback chain:
        // 1. In recovery AP mode, primary fragment wins over any hash (the wizard is
        //    the only meaningful page; a stale #welcome from a prior session
        //    must not override it).
        // 2. Otherwise: URL hash if present and exists in visible nav
        // 3. Primary fragment if defined and exists in visible nav
        // 4. #welcome
        var itemId = null;
        if ((data.recovery_ap ?? data.ap_mode) && primary && primary.fragment && itemMap[primary.fragment]) {
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
        navigateTo(itemId, getItemFromHash() === itemId);

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
