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
  var healthPanel = document.getElementById('health-panel');
  var healthClose = document.getElementById('health-close');

  var categories = [];
  var itemMap = {};       // itemId → { cat, item }
  var currentItem = null;
  var activeFetchController = null;
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
    setActive(itemId);
    expandCategoryFor(itemId);

    // Cancel any in-flight fetch
    if (activeFetchController) activeFetchController.abort();
    activeFetchController = new AbortController();
    var signal = activeFetchController.signal;

    var timeoutId = setTimeout(function () {
      activeFetchController.abort();
    }, FETCH_TIMEOUT_MS);

    // Show loading
    contentEl.innerHTML =
      '<div class="fragment-loading">' +
      '<div class="spinner-border text-primary" role="status">' +
      '<span class="visually-hidden">Loading…</span></div>' +
      '<small class="text-muted">Loading section…</small></div>';

    fetch(SECTION_API + itemId, { signal: signal })
      .then(function (res) {
        clearTimeout(timeoutId);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.text();
      })
      .then(function (html) {
        contentEl.innerHTML = html;
        // Convention-based fragment init
        var initFn = window['init_' + itemId.replace(/-/g, '_') + '_fragment'];
        if (typeof initFn === 'function') initFn();
      })
      .catch(function (err) {
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

  // ---------- Health panel ----------

  function toggleHealthPanel() {
    if (healthPanel.style.display === 'none') {
      healthPanel.style.display = '';
    } else {
      healthPanel.style.display = 'none';
    }
  }

  if (healthBadge) {
    healthBadge.addEventListener('click', toggleHealthPanel);
  }

  if (healthClose) {
    healthClose.addEventListener('click', function () {
      healthPanel.style.display = 'none';
    });
  }

  // Close health panel on click outside
  document.addEventListener('click', function (e) {
    if (healthPanel.style.display !== 'none' &&
        !healthPanel.contains(e.target) &&
        !healthBadge.contains(e.target)) {
      healthPanel.style.display = 'none';
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
        buildNav(data.categories || []);

        // Load item from hash or default to first item of first category
        var itemId = getItemFromHash();
        if (!itemId) {
          itemId = 'welcome';
        }
        if (itemId) navigateTo(itemId);
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
