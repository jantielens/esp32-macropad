const portalExperiments = [
  {id:'01-signal', name:'Signal', note:'Precision grotesk, vermilion signals, crisp neutral surfaces.', font:'"Space Grotesk"', heading:'"Space Grotesk"', radius:4, weight:600, style:'signal', light:['#f1f2f3','#ffffff','#e7e9ec','#16191d','#565d67','#c7ccd2','#be3420','#ffffff'], dark:['#151719','#202326','#2b3034','#f4f5f6','#aeb6bf','#454c54','#ff826b','#191b1e']},
  {id:'02-fieldwork', name:'Fieldwork', note:'Humanist green, generous rhythm, quiet tactile surfaces.', font:'"Source Sans 3"', heading:'"Manrope"', radius:8, weight:600, style:'fieldwork', light:['#eef3f1','#ffffff','#e4ede8','#16352b','#52665d','#bfd0c5','#146745','#ffffff'], dark:['#151c19','#202b25','#2b3930','#edf5ef','#acbfb2','#42594a','#8ed7ad','#19221c']},
  {id:'03-terminal', name:'Terminal', note:'Monospaced instrumentation, cyan and amber, etched grid.', font:'"IBM Plex Mono"', heading:'"IBM Plex Mono"', radius:2, weight:500, style:'terminal', light:['#edf2f4','#ffffff','#e0e9ee','#142b34','#4d6571','#afc3cd','#006784','#ffffff'], dark:['#0e161b','#15232c','#203440','#e3f3f8','#9bb8c6','#355564','#65dbed','#102028']},
  {id:'04-punch', name:'Punch', note:'Graphic red, heavyweight condensed headings, offset ink edges.', font:'"Barlow"', heading:'"Barlow Condensed"', radius:0, weight:700, style:'punch', light:['#f2f2f0','#ffffff','#e9e8e3','#202022','#606064','#29292b','#b51838','#ffffff'], dark:['#1c191c','#292329','#382f37','#fff4f7','#cbb5c0','#79616e','#ff89a6','#211920']},
  {id:'05-mono', name:'Mono', note:'Restrained monochrome, typographic hierarchy, almost no ornament.', font:'"DM Sans"', heading:'"DM Sans"', radius:3, weight:500, style:'mono', light:['#f7f7f7','#ffffff','#eeeeee','#1b1b1b','#616161','#d0d0d0','#252525','#ffffff'], dark:['#151515','#202020','#2d2d2d','#f4f4f4','#b5b5b5','#484848','#e5e5e5','#171717']},
  {id:'06-cobalt', name:'Cobalt', note:'Architectural blue, sharp white planes, confident display type.', font:'"Manrope"', heading:'"Space Grotesk"', radius:5, weight:600, style:'cobalt', light:['#eef1f8','#ffffff','#e1e8f6','#17244a','#526180','#bfcae1','#214cce','#ffffff'], dark:['#14161e','#202431','#2c3245','#f0f3ff','#aeb9d4','#49546e','#91b2ff','#172139']},
  {id:'07-editorial', name:'Editorial', note:'Unexpected serif headings, wine accents, fine publishing rules.', font:'"Source Sans 3"', heading:'"Bitter"', radius:3, weight:600, style:'editorial', light:['#f3f2f4','#ffffff','#ebe6ed','#302330','#6e5d6e','#cfbfce','#87335e','#ffffff'], dark:['#1e1a20','#2b242e','#3a2f3c','#f7eff8','#c4aec8','#645169','#eea3ce','#251c27']},
  {id:'08-voltage', name:'Voltage', note:'Industrial yellow, black rails, uppercase equipment labels.', font:'"Barlow"', heading:'"Chakra Petch"', radius:2, weight:600, style:'voltage', light:['#eeefeb','#ffffff','#e4e6de','#25291d','#61684c','#bac0a9','#586800','#ffffff'], dark:['#191b16','#25291f','#353b2a','#f3f7e7','#bfc8a4','#576242','#d3ef67','#212619']},
  {id:'09-tidal', name:'Tidal', note:'Clear cyan, rounded humanist type, subtle soft elevation.', font:'"Nunito Sans"', heading:'"Nunito Sans"', radius:8, weight:600, style:'tidal', light:['#eff5f6','#ffffff','#e0edef','#153b44','#53717a','#b8d2d7','#006f82','#ffffff'], dark:['#151d20','#202d33','#2b3e46','#effafd','#a6c1ca','#43616b','#7bdae8','#142d33']},
  {id:'10-mosaic', name:'Mosaic', note:'Multicolor wayfinding, squared surfaces, playful but operational.', font:'"IBM Plex Sans"', heading:'"Space Grotesk"', radius:4, weight:600, style:'mosaic', light:['#f1f2f5','#ffffff','#e6e8ef','#262b3a','#626b80','#c4c9d8','#7045a1','#ffffff'], dark:['#1c1b23','#292733','#383545','#f7f3ff','#beb4d0','#615772','#d3a9fa','#291f35']},
  {id:'11-console', name:'Console', group:'style', note:'Dense 12px mono controls, narrow gutters, compact nav, hairline instrumentation.', font:'"IBM Plex Mono"', heading:'"IBM Plex Mono"', radius:1, weight:500, stroke:1.3, style:'console', light:['#f0f2f2','#ffffff','#e4e8e7','#20302c','#536760','#b6c4bf','#14664f','#ffffff'], dark:['#131917','#1d2722','#29362e','#e9f5ed','#acc2b3','#486053','#9adab6','#17251b']},
  {id:'12-studio', name:'Studio', group:'style', note:'Airy 17px humanist text, broad gutters, quiet headers, generous form rhythm.', font:'"Source Sans 3"', heading:'"Manrope"', radius:8, weight:600, stroke:1.5, style:'studio', light:['#f3f5f7','#ffffff','#e9edf1','#222f3b','#5e6b79','#c7d0da','#37657e','#ffffff'], dark:['#191d21','#242c33','#303b45','#eff7fc','#b6c5d1','#4e626f','#a0cde4','#152934']},
  {id:'13-swiss', name:'Swiss', group:'style', note:'32px grotesk headings, square planes, red/black rules, emphatic section hierarchy.', font:'"DM Sans"', heading:'"Space Grotesk"', radius:0, weight:700, stroke:2.2, style:'swiss', light:['#f5f5f3','#ffffff','#e9e9e6','#181818','#5f5f5a','#babab4','#c22922','#ffffff'], dark:['#191919','#252525','#333333','#fafafa','#c1c1b9','#61615a','#ff9286','#211613']},
  {id:'14-ledger', name:'Ledger', group:'style', note:'Serif-led reading, open sections, double rules, italic captions, no floating panels.', font:'"Source Sans 3"', heading:'"Bitter"', radius:0, weight:600, stroke:1.3, style:'ledger', light:['#f4f3f5','#f4f3f5','#eae6ed','#342635','#716173','#c9bdcc','#814466','#ffffff'], dark:['#211d23','#211d23','#322a36','#f6eef8','#c6b5ca','#67576d','#eab0d7','#261b29']},
  {id:'15-outline', name:'Outline', group:'style', note:'Borderless section bands, thin icon strokes, understated 14px type, underline inputs.', font:'"Manrope"', heading:'"Manrope"', radius:0, weight:500, stroke:1, style:'outline', light:['#ffffff','#ffffff','#f3f5f5','#273330','#66746e','#c8d1cc','#3d6655','#ffffff'], dark:['#1a1e1c','#1a1e1c','#272e29','#f0f6f1','#b3c2b6','#4b5b50','#b4d5bf','#1c2b21']},
  {id:'16-spectrum', name:'Spectrum', group:'style', note:'Cyan/coral/violet/green section bands, colored icon squares, strong multicolor wayfinding.', font:'"IBM Plex Sans"', heading:'"Space Grotesk"', radius:4, weight:600, stroke:2, style:'spectrum', light:['#f1f3f6','#ffffff','#e6ebf1','#202a3b','#607089','#c4cedb','#355fa3','#ffffff'], dark:['#191d25','#242c39','#344050','#f1f6ff','#b3c2d7','#566782','#a0c5fc','#1d2d44']},
  {id:'17-hardware', name:'Hardware', group:'style', note:'Etched panels, compact technical caps, amber caution stripe, inset controls and chunky switches.', font:'"Barlow"', heading:'"Chakra Petch"', radius:2, weight:700, stroke:2.4, style:'hardware', light:['#ecefed','#f8faf8','#dce3dd','#27362b','#5a6c5d','#a4b6a8','#536526','#ffffff'], dark:['#1a201a','#252f25','#374535','#f3f7e9','#bfceb4','#607658','#d8e8a0','#26301c']},
  {id:'18-poster', name:'Poster', group:'style', note:'Condensed 34px titles, right-aligned headings, wide breathing room, bold colored rules.', font:'"Barlow"', heading:'"Barlow Condensed"', radius:0, weight:700, stroke:2.5, style:'poster', light:['#f3f1f3','#ffffff','#eae3e9','#2c202a','#735d6f','#cbbcc8','#a82e5c','#ffffff'], dark:['#211a20','#2e242c','#43303c','#fff0fa','#d2b4c9','#76596d','#ffa1ce','#301c28']},
  {id:'19-wayfinder', name:'Wayfinder', group:'style', note:'Color-coded category rails, bolder navigation, asymmetric heading accents, neutral workspace.', font:'"IBM Plex Sans"', heading:'"DM Sans"', radius:3, weight:600, stroke:2, style:'wayfinder', light:['#f2f3f4','#ffffff','#e7e9ec','#242b32','#626e78','#c5ccd2','#34667c','#ffffff'], dark:['#1b1e22','#272c32','#383f48','#f3f7fb','#b6c1cc','#586574','#9bcee5','#202e37']},
  {id:'20-soft', name:'Soft', group:'style', note:'Rounded humanist 16px text, softly raised controls, roomy labels, gentle 8px surfaces.', font:'"Nunito Sans"', heading:'"Nunito Sans"', radius:8, weight:700, stroke:1.8, style:'soft', light:['#edf3f4','#f8fcfd','#e0eaed','#25414b','#647d87','#bfced4','#286a81','#ffffff'], dark:['#1a2227','#27343c','#344650','#effaff','#b2ccd7','#537180','#9ed9ee','#233d48']},
  {id:'21-ink', name:'Ink', group:'style', note:'Heavy monochrome borders, bold icon stamps, offset control edges, unapologetic graphic contrast.', font:'"Space Grotesk"', heading:'"Space Grotesk"', radius:0, weight:700, stroke:3, style:'ink', light:['#f1f1ef','#ffffff','#e3e3df','#171717','#595953','#242424','#242424','#ffffff'], dark:['#181818','#252525','#373737','#f7f7f3','#c6c6bd','#d2d2c7','#eeeeea','#181818']},
  {id:'22-playroom', name:'Playroom', group:'style', note:'Real color emoji, rounded 16px type, vivid category accents, friendly spacious controls.', font:'"Nunito Sans"', heading:'"Nunito Sans"', radius:8, weight:700, iconMode:'emoji', style:'playroom', light:['#f3f5f8','#ffffff','#e7edf4','#243347','#647792','#bdcce0','#3464af','#ffffff'], dark:['#1b202a','#283345','#3a4b63','#f1f7ff','#b7cae5','#5b7296','#a8cdff','#203a5e']}
];
window.portalExperiments = portalExperiments;
window.portalMobileExperimentIds = ['01-signal','03-terminal','04-punch','09-tidal','11-console','12-studio','16-spectrum','18-poster','22-playroom'];

function experimentCSS(theme, mode) {
  const [bg,surface,alt,text,muted,border,accent,onAccent] = theme[mode];
  const rgb = accent.match(/[a-f0-9]{2}/gi).map(value => parseInt(value,16)).join(',');
  const tokens = {
    'bs-body-bg':bg, 'bs-body-color':text, 'bs-secondary-color':muted, 'bs-tertiary-bg':alt,
    'bs-border-color':border, 'bs-primary':accent, 'bs-primary-rgb':rgb, 'bs-link-color':accent,
    'bs-link-color-rgb':rgb, 'bs-link-hover-color':accent, 'bs-emphasis-color':text,
    'portal-primary':accent, 'portal-primary-rgb':rgb, 'portal-sidebar-bg':alt,
    'portal-card-bg':surface, 'portal-card-border':border, 'portal-card-header-bg':alt,
    'portal-card-hover-border':accent, 'portal-text-muted':muted, 'portal-nav-border':border,
    'portal-nav-hover-bg':surface, 'portal-nav-item-hover-bg':surface, 'portal-form-bg':surface,
    'portal-form-border':border, 'portal-bar-track':alt, 'portal-scrollbar-thumb':border,
    'portal-welcome-card-bg':surface, 'portal-welcome-card-border':border,
    'portal-health-bg':surface, 'portal-health-border':border, 'portal-health-sparkline-bg':alt,
    'portal-health-sparkline-border':border, 'portal-surface':surface, 'portal-surface-alt':alt,
    'portal-surface-hover':alt, 'portal-text':text, 'portal-border':border, 'portal-border-light':border,
    'portal-editor-bg':bg, 'portal-editor-nested-bg':alt, 'portal-editor-nested-accent':accent,
    'portal-blue':accent, 'portal-blue-hover':accent, 'portal-link-blue':accent,
    'portal-docs-bg':surface, 'portal-docs-border':border, 'portal-docs-header-bg':alt,
    'portal-docs-heading':text, 'portal-docs-body':muted, 'portal-docs-badge-bg':alt,
    'portal-docs-badge-color':accent, 'portal-docs-spec-bg':surface, 'portal-docs-code-bg':alt,
    'portal-docs-code-color':text, 'lab-accent-ink':onAccent, 'lab-radius':theme.radius+'px',
    'lab-font':theme.font+', sans-serif', 'lab-heading':theme.heading+', sans-serif',
    'lab-weight':theme.weight, 'lab-bg':bg,
    'lab-secondary':mode==='light'?'#a54865':'#efa1be',
    'lab-tertiary':mode==='light'?'#267b6b':'#8bd7bb',
    'lab-fourth':mode==='light'?'#7663a1':'#c7b4f0'
  };
  return `:root[data-bs-theme="${mode}"]{${Object.entries(tokens).map(([key,value])=>`--${key}:${value}`).join(';')}}`;
}

if (document.querySelector('.portal-header')) {
  const params = new URLSearchParams(location.search);
  const selected = portalExperiments.find(theme => theme.id === params.get('variant')) || portalExperiments[0];
  document.documentElement.dataset.experiment = selected.style;
  document.documentElement.dataset.experimentGroup = selected.group || 'palette';
  const style = document.createElement('style');
  style.textContent = experimentCSS(selected,'light') + experimentCSS(selected,'dark');
  document.head.append(style);
  const select = document.createElement('select');
  select.id = 'experiment-select';
  select.setAttribute('aria-label','Visual theme experiment');
  select.title = 'Visual theme experiment';
  select.innerHTML = portalExperiments.map(theme=>`<option value="${theme.id}">${theme.name}</option>`).join('');
  select.value = selected.id;
  select.addEventListener('change',()=> {
    const url = new URL(location.href);
    url.searchParams.set('variant',select.value);
    location.href = url;
  });
  document.querySelector('.portal-header-top').append(select);
  const icons = document.createElement('script');
  icons.src = '/lab/lucide.min.js';
  icons.onload = () => {
    const names = {
      0x1f3e0:'house', 0x1f527:'wrench', 0x1f5a5:'monitor', 0x1f4f1:'layout-grid',
      0x26a1:'zap', 0x1f310:'network', 0x1f50a:'volume-2', 0x1f4be:'hard-drive',
      0x1f4e1:'radio', 0x1f319:'moon', 0x2600:'sun', 0x1f504:'rotate-cw',
      0x1f44b:'hand', 0x1f4f6:'wifi', 0x1f4a1:'lightbulb', 0x1f4e6:'package',
      0x1f680:'upload', 0x1f4da:'book-open', 0x2699:'settings', 0x1f5bc:'image',
      0x2328:'keyboard', 0x1f3ae:'gamepad-2', 0x1f5b1:'mouse', 0x1f4a4:'moon',
      0x26a0:'triangle-alert', 0x1f50c:'plug', 0x1f50d:'search', 0x1f4cb:'clipboard',
      0x1f512:'lock', 0x1f511:'key-round', 0x1f4dd:'square-pen', 0x1f4ca:'chart-no-axes-combined',
      0x1f39b:'sliders-horizontal', 0x1f4d6:'book-open', 0x1f517:'external-link', 0x1f4bb:'laptop'
    };
    const observer = new MutationObserver(replaceIcons);
    function replaceIcons() {
      observer.disconnect();
      const walker = document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);
      const nodes = [];
      while(walker.nextNode()) {
        const node=walker.currentNode;
        if(!node.parentElement.closest('script,style,select,textarea,svg,#pad-workspace-canvas') && /\p{Extended_Pictographic}/u.test(node.textContent)) nodes.push(node);
      }
      for(const node of nodes) {
        const parts = node.textContent.split(/((?:\p{Extended_Pictographic}[\uFE0F\u200D]?)+)/u);
        const fragment = document.createDocumentFragment();
        for(const part of parts) {
          if(/\p{Extended_Pictographic}/u.test(part)) {
            const icon = document.createElement(selected.iconMode==='emoji'?'span':'i');
            if(selected.iconMode==='emoji') icon.textContent=part;
            else icon.setAttribute('data-lucide',names[part.codePointAt(0)] || 'circle-help');
            icon.setAttribute('aria-hidden','true');
            icon.className=selected.iconMode==='emoji'?'lab-emoji':'lab-icon';
            fragment.append(icon);
          } else fragment.append(document.createTextNode(part));
        }
        node.replaceWith(fragment);
      }
      lucide.createIcons({attrs:{'stroke-width':selected.stroke || (selected.style==='punch'?2.5:1.7)}});
      document.documentElement.dataset.iconsReady='true';
      observer.observe(document.body,{childList:true,subtree:true});
    }
    replaceIcons();
  };
  document.head.append(icons);
}