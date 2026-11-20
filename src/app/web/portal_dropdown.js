// portal_dropdown.js — Custom dropdown that replaces native <select>.
//
// Why: native <select> popups are rendered by the OS/browser and can't be
// themed reliably (white background in dark mode, glitchy open/close on
// some platforms). This helper hides the original <select> but keeps it
// fully functional so all existing JS (el.value, el.options, change events)
// continues to work unchanged.
//
// Usage:
//   dropdownEnhanceAll(rootEl)   // convert every <select> under rootEl
//   dropdownEnhance(selectEl)    // convert a single <select>
//   dropdownRefresh(selectEl)    // call after mutating <option> children
//
// A MutationObserver also auto-syncs the visible UI when option lists or
// the selected value change programmatically, so most call sites need no
// extra wiring.

(function () {
    'use strict';

    var openDropdown = null; // currently open custom dropdown wrapper

    function closeAny(restoreFocus) {
        if (openDropdown) {
            var toggle = openDropdown.querySelector('.cdd-toggle');
            var search = openDropdown.querySelector('.cdd-search');
            openDropdown.classList.remove('cdd-open');
            toggle.setAttribute('aria-expanded', 'false');
            if (search) {
                search.setAttribute('aria-expanded', 'false');
                search.removeAttribute('aria-activedescendant');
            }
            openDropdown = null;
            if (restoreFocus) toggle.focus();
        }
    }

    document.addEventListener('click', function (e) {
        if (!openDropdown) return;
        if (!openDropdown.contains(e.target)) closeAny();
    });

    document.addEventListener('keydown', function (e) {
        if (e.key === 'Escape') closeAny();
    });

    function getSelectedOption(sel) {
        if (sel.selectedIndex < 0) return null;
        return sel.options[sel.selectedIndex] || null;
    }

    function renderToggleLabel(sel, toggleEl) {
        var opt = getSelectedOption(sel);
        var text = opt ? opt.textContent : '';
        toggleEl.textContent = text || '\u00a0';
        toggleEl.classList.toggle('cdd-placeholder', !text || (!!sel.dataset.searchPlaceholder && !sel.value));
    }

    function renderMenu(sel, menuEl) {
        var search = menuEl.querySelector('.cdd-search');
        var list = search ? menuEl.querySelector('.cdd-results') : menuEl;
        var query = search ? search.value.trim().toLowerCase() : '';
        list.innerHTML = '';
        var flatIndex = 0;
        var count = 0;
        function matches(opt, groupLabel) {
            return !opt.hidden && (!search || !!opt.value) &&
                (!query || (opt.textContent + ' ' + opt.value + ' ' + (groupLabel || '')).toLowerCase().includes(query));
        }
        function renderOption(opt, grouped) {
            if (!matches(opt, grouped)) {
                flatIndex++;
                return;
            }
            var item = document.createElement('button');
            item.type = 'button';
            item.className = grouped ? 'cdd-item cdd-item-grouped' : 'cdd-item';
            if (opt.disabled) item.classList.add('cdd-disabled');
            if (flatIndex === sel.selectedIndex) item.classList.add('cdd-selected');
            item.textContent = opt.textContent;
            item.dataset.cddIndex = String(flatIndex);
            if (search) {
                item.id = sel.id + '-choice-' + flatIndex;
                item.setAttribute('role', 'option');
                item.setAttribute('aria-selected', String(flatIndex === sel.selectedIndex));
                item.setAttribute('aria-disabled', String(opt.disabled));
                item.tabIndex = -1;
            }
            list.appendChild(item);
            count++;
            flatIndex++;
        }
        // Walk top-level children so <optgroup> renders a non-interactive
        // header, matching native <select> popup behavior. sel.selectedIndex
        // and the dataset.cddIndex click handler both use the flat option
        // index (same as sel.options), so flatIndex must count every
        // <option> exactly once regardless of optgroup nesting.
        for (var i = 0; i < sel.children.length; i++) {
            var node = sel.children[i];
            if (node.tagName === 'OPTGROUP') {
                if (node.hidden || !Array.from(node.children).some(function(opt) { return matches(opt, node.label); })) {
                    flatIndex += node.children.length;
                    continue;
                }
                var header = document.createElement('div');
                header.className = 'cdd-group-label';
                header.textContent = node.label;
                list.appendChild(header);
                for (var j = 0; j < node.children.length; j++) renderOption(node.children[j], node.label);
            } else if (node.tagName === 'OPTION') {
                renderOption(node);
            }
        }
        if (search) {
            menuEl.querySelector('.cdd-status').textContent = count
                ? (query ? count + ' matching action' + (count === 1 ? '' : 's') : '')
                : 'No actions found';
            menuEl.querySelector('.cdd-clear-search').hidden = !search.value;
            setActive(menuEl, null);
        }
    }

    function setActive(menu, item) {
        menu.querySelectorAll('.cdd-active').forEach(function(node) { node.classList.remove('cdd-active'); });
        var search = menu.querySelector('.cdd-search');
        if (!search) return;
        if (item) {
            item.classList.add('cdd-active');
            search.setAttribute('aria-activedescendant', item.id);
            item.scrollIntoView({ block: 'nearest' });
        } else {
            search.removeAttribute('aria-activedescendant');
        }
    }

    function buildWrapper(sel) {
        var wrap = document.createElement('div');
        wrap.className = 'cdd-wrap';
        if (sel.classList.contains('form-select-sm')) wrap.classList.add('cdd-sm');
        if (sel.disabled) wrap.classList.add('cdd-disabled');

        var toggle = document.createElement('button');
        toggle.type = 'button';
        toggle.className = 'cdd-toggle';
        toggle.setAttribute('aria-haspopup', 'listbox');
        toggle.setAttribute('aria-expanded', 'false');

        var menu = document.createElement('div');
        menu.className = 'cdd-menu';
        if (sel.dataset.searchPlaceholder) {
            wrap.classList.add('cdd-searchable');
            var tools = document.createElement('div');
            tools.className = 'cdd-search-tools';
            var search = document.createElement('input');
            search.type = 'search';
            search.className = 'form-control form-control-sm cdd-search action-operation-search';
            search.placeholder = sel.dataset.searchPlaceholder;
            search.setAttribute('aria-label', 'Search actions');
            search.setAttribute('role', 'combobox');
            search.setAttribute('aria-autocomplete', 'list');
            search.setAttribute('aria-expanded', 'false');
            var results = document.createElement('div');
            results.className = 'cdd-results';
            results.id = sel.id + '-choices';
            results.setAttribute('role', 'listbox');
            results.setAttribute('aria-label', 'Actions');
            search.setAttribute('aria-controls', results.id);
            var status = document.createElement('small');
            status.className = 'cdd-status';
            status.setAttribute('role', 'status');
            var clear = document.createElement('button');
            clear.type = 'button';
            clear.className = 'btn btn-sm btn-outline-secondary cdd-clear-search';
            clear.textContent = 'Clear';
            clear.setAttribute('aria-label', 'Clear search');
            tools.appendChild(search);
            tools.appendChild(clear);
            menu.appendChild(tools);
            menu.appendChild(status);
            menu.appendChild(results);
            toggle.setAttribute('aria-controls', results.id);
        } else {
            menu.setAttribute('role', 'listbox');
        }

        wrap.appendChild(toggle);
        wrap.appendChild(menu);
        return { wrap: wrap, toggle: toggle, menu: menu };
    }

    function attach(sel) {
        if (sel.dataset.cddInit === '1') return;
        sel.dataset.cddInit = '1';

        var built = buildWrapper(sel);
        var wrap = built.wrap;
        var toggle = built.toggle;
        var menu = built.menu;

        // Insert wrapper before the select, then move select inside (hidden).
        sel.parentNode.insertBefore(wrap, sel);
        wrap.appendChild(sel);
        sel.classList.add('cdd-hidden');
        var search = menu.querySelector('.cdd-search');
        if (search) {
            sel.tabIndex = -1;
            sel.setAttribute('aria-hidden', 'true');
        }

        renderMenu(sel, menu);
        renderToggleLabel(sel, toggle);

        function openMenu() {
            if (sel.disabled) return;
            closeAny();
            if (search) {
                search.value = '';
                menu.querySelector('.cdd-results').style.maxHeight = '';
            }
            // Refresh the menu in case options changed since last render.
            renderMenu(sel, menu);
            wrap.classList.add('cdd-open');
            toggle.setAttribute('aria-expanded', 'true');
            openDropdown = wrap;
            // Position menu: flip up if not enough space below.
            menu.style.top = '';
            menu.style.bottom = '';
            var rect = toggle.getBoundingClientRect();
            var spaceBelow = window.innerHeight - rect.bottom;
            var flip = search
                ? spaceBelow < menu.getBoundingClientRect().height && rect.top > spaceBelow
                : spaceBelow < 220 && rect.top > 220;
            if (flip) {
                menu.style.bottom = '100%';
                menu.style.top = 'auto';
            }
            if (search) {
                var results = menu.querySelector('.cdd-results');
                var overhead = menu.getBoundingClientRect().height - results.getBoundingClientRect().height;
                results.style.maxHeight = Math.max(0, Math.min(240, window.innerHeight * 0.35,
                    (flip ? rect.top : spaceBelow) - overhead - 8)) + 'px';
                search.setAttribute('aria-expanded', 'true');
                search.focus();
            }
        }

        toggle.addEventListener('click', function (e) {
            e.stopPropagation();
            if (wrap.classList.contains('cdd-open')) {
                closeAny();
                return;
            }
            openMenu();
        });

        if (search) {
            ['input', 'change'].forEach(function(event) {
                search.addEventListener(event, function(e) {
                    e.stopPropagation();
                    if (event === 'input') renderMenu(sel, menu);
                });
            });
            menu.querySelector('.cdd-clear-search').addEventListener('click', function() {
                search.focus();
                search.value = '';
                renderMenu(sel, menu);
            });
            toggle.addEventListener('keydown', function(e) {
                if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
                e.preventDefault();
                openMenu();
            });
            wrap.addEventListener('keydown', function(e) {
                if (!wrap.classList.contains('cdd-open')) return;
                if (e.key === 'Escape') {
                    e.preventDefault();
                    e.stopPropagation();
                    closeAny(true);
                } else if (e.target === search) {
                    var items = Array.from(menu.querySelectorAll('.cdd-item:not(.cdd-disabled)'));
                    var active = menu.querySelector('.cdd-active');
                    var index = items.indexOf(active);
                    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                        e.preventDefault();
                        var next = e.key === 'ArrowDown' ? index + 1 : (index < 0 ? items.length - 1 : index - 1);
                        setActive(menu, items[(next + items.length) % items.length]);
                    } else if (e.key === 'Enter') {
                        e.preventDefault();
                        e.stopPropagation();
                        if (active) active.click();
                    }
                }
            });
            wrap.addEventListener('focusout', function(e) {
                if (!wrap.contains(e.relatedTarget) && openDropdown === wrap) closeAny();
            });
        }

        menu.addEventListener('click', function (e) {
            var item = e.target.closest('.cdd-item');
            if (!item) return;
            if (item.classList.contains('cdd-disabled')) return;
            var idx = parseInt(item.dataset.cddIndex, 10);
            if (isNaN(idx)) return;
            if (search) search.focus();
            if (sel.selectedIndex !== idx) {
                sel.selectedIndex = idx;
                // Fire native change event so existing onchange handlers run.
                sel.dispatchEvent(new Event('change', { bubbles: true }));
            }
            renderToggleLabel(sel, toggle);
            renderMenu(sel, menu);
            closeAny(!!search);
        });

        // Mirror disabled state.
        var disabledObserver = new MutationObserver(function () {
            wrap.classList.toggle('cdd-disabled', sel.disabled);
            toggle.disabled = sel.disabled;
            if (sel.disabled && openDropdown === wrap) closeAny();
        });
        disabledObserver.observe(sel, { attributes: true, attributeFilter: ['disabled'] });

        // Sync UI when options change (e.g. dynamic populate via innerHTML / appendChild).
        var optsObserver = new MutationObserver(function () {
            renderMenu(sel, menu);
            renderToggleLabel(sel, toggle);
        });
        optsObserver.observe(sel, { childList: true, subtree: false });

        // Sync UI when value is set programmatically.  Native <select> doesn't
        // fire 'change' on programmatic value writes, so we monkey-patch the
        // 'value' setter for this instance.
        var proto = Object.getPrototypeOf(sel);
        var desc = Object.getOwnPropertyDescriptor(proto, 'value');
        if (desc && desc.set) {
            Object.defineProperty(sel, 'value', {
                get: desc.get,
                set: function (v) {
                    desc.set.call(this, v);
                    renderToggleLabel(sel, toggle);
                    renderMenu(sel, menu);
                },
                configurable: true
            });
        }

        // Save references on the select for refresh().
        sel._cddToggle = toggle;
        sel._cddMenu = menu;
    }

    function refresh(sel) {
        if (sel.dataset.cddInit !== '1') return;
        renderMenu(sel, sel._cddMenu);
        renderToggleLabel(sel, sel._cddToggle);
    }

    function enhance(sel) {
        if (!(sel instanceof HTMLSelectElement)) return;
        if (sel.multiple) return; // multi-select: keep native UI
        attach(sel);
    }

    function enhanceAll(root) {
        var scope = root || document;
        var nodes = scope.querySelectorAll('select');
        for (var i = 0; i < nodes.length; i++) enhance(nodes[i]);
    }

    // Expose API.
    window.dropdownEnhance = enhance;
    window.dropdownEnhanceAll = enhanceAll;
    window.dropdownRefresh = refresh;

    // Auto-enhance any <select> added to the document later (e.g. action
    // editors, timer editors, swipe editors that build HTML via innerHTML
    // after fragment init).  This keeps call sites unchanged.
    var globalObserver = new MutationObserver(function (mutations) {
        for (var m = 0; m < mutations.length; m++) {
            var added = mutations[m].addedNodes;
            for (var i = 0; i < added.length; i++) {
                var node = added[i];
                if (node.nodeType !== 1) continue;
                if (node.tagName === 'SELECT') {
                    enhance(node);
                } else if (node.querySelectorAll) {
                    var inner = node.querySelectorAll('select');
                    for (var j = 0; j < inner.length; j++) enhance(inner[j]);
                }
            }
        }
    });

    function startGlobalObserver() {
        globalObserver.observe(document.body, { childList: true, subtree: true });
    }
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', startGlobalObserver);
    } else {
        startGlobalObserver();
    }
})();
