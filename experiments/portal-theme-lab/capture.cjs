const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const base = process.env.LAB_URL || 'http://localhost:8777';
const fragments = ['welcome','pad-editor','screensaver','hid'];

(async()=> {
  const browser = await chromium.launch({headless:true});
  const context = await browser.newContext({viewport:{width:1600,height:1100}, reducedMotion:'reduce'});
  const page = await context.newPage();
  await page.goto(base+'/lab');
  const configResponse = await context.request.post(base+'/api/config?no_reboot=1',{data:{keyboard_transport:'usb'}});
  const rebootResponse = await context.request.post(base+'/api/reboot');
  if(!configResponse.ok() || !rebootResponse.ok()) throw new Error('Mock HID fixture setup failed');
  const allThemes = await page.evaluate(()=>window.portalExperiments);
  const themes = allThemes.filter(theme=>(!process.env.LAB_VARIANT || theme.id===process.env.LAB_VARIANT) && (!process.env.LAB_COLLECTION || theme.group===process.env.LAB_COLLECTION));
  const mobileIds = await page.evaluate(()=>window.portalMobileExperimentIds);
  await fs.mkdir(path.join(__dirname,'screenshots'),{recursive:true});
  const results = [];
  for (const theme of themes) {
    for (const mode of ['light','dark']) {
      for (const fragment of fragments) {
        await page.evaluate(mode=>localStorage.setItem('portal-theme',mode),mode);
        await page.goto(`${base}/?profile=jc3248w535&variant=${theme.id}&fragment=${fragment}`);
        await page.waitForFunction(()=>document.querySelector('#content-pane')?.innerText.length>100 && !document.querySelector('.fragment-loading'),undefined,{polling:100});
        await page.waitForFunction(()=>document.documentElement.dataset.iconsReady==='true',undefined,{polling:100});
        await page.evaluate(()=>document.fonts.ready);
        await page.waitForFunction(()=>document.querySelector('#firmware-version')?.innerText !== 'Firmware v-.-.-',undefined,{polling:100});
        await page.evaluate(()=>new Promise(resolve=>setTimeout(resolve,600)));
        const check = await page.evaluate(()=>({theme:document.documentElement.dataset.bsTheme,overflow:document.documentElement.scrollWidth>innerWidth,content:document.querySelector('#content-pane').innerText.slice(0,100),error:!!document.querySelector('.fragment-error'),fonts:document.fonts.check('14px '+getComputedStyle(document.body).fontFamily)}));
        if (check.theme!==mode || check.error || check.overflow || !check.fonts) throw new Error(JSON.stringify({theme:theme.id,mode,fragment,...check}));
        const filename = `${theme.id}-${mode}-${fragment}.png`;
        await page.screenshot({path:path.join(__dirname,'screenshots',filename),fullPage:true});
        results.push({variant:theme.id,mode,fragment,viewport:'desktop',file:filename,...check});
        if (fragment==='welcome') {
          await page.locator('#theme-toggle').click();
          if(await page.getAttribute('html','data-bs-theme')===mode) throw new Error('Theme toggle failed');
        }
      }
    }
    console.log('Captured desktop: '+theme.name);
  }
  await page.setViewportSize({width:390,height:844});
  for (const theme of themes.filter(theme=>mobileIds.includes(theme.id))) {
    for (const mode of ['light','dark']) {
      for (const fragment of ['welcome','screensaver']) {
        await page.evaluate(mode=>localStorage.setItem('portal-theme',mode),mode);
        await page.goto(`${base}/?profile=jc3248w535&variant=${theme.id}&fragment=${fragment}`);
        await page.waitForFunction(()=>document.querySelector('#content-pane')?.innerText.length>100 && !document.querySelector('.fragment-loading'),undefined,{polling:100});
        await page.waitForFunction(()=>document.documentElement.dataset.iconsReady==='true',undefined,{polling:100});
        await page.evaluate(()=>document.fonts.ready);
        await page.evaluate(()=>new Promise(resolve=>setTimeout(resolve,600)));
        const overflow = await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
        if(overflow) throw new Error('Mobile overflow: '+theme.id);
        const filename = `${theme.id}-${mode}-${fragment}-mobile.png`;
        await page.screenshot({path:path.join(__dirname,'screenshots',filename),fullPage:true});
        results.push({variant:theme.id,mode,fragment,viewport:'mobile',file:filename,overflow});
      }
    }
    console.log('Captured mobile: '+theme.name);
  }
  let previous=[];
  try { previous=JSON.parse(await fs.readFile(path.join(__dirname,'results.json'),'utf8')); } catch(error) { if(error.code!=='ENOENT') throw error; }
  const captured=new Set(results.map(result=>result.file));
  await fs.writeFile(path.join(__dirname,'results.json'),JSON.stringify([...previous.filter(result=>!captured.has(result.file)),...results],null,2));
  await browser.close();
  console.log(`Finished: ${results.length} screenshots; light/dark toggles and overflow checked.`);
})().catch(error=>{console.error(error);process.exit(1)});