const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const zlib = require('node:zlib');
const {execFileSync} = require('node:child_process');
const {chromium} = require('playwright');
const root = process.cwd();
fs.mkdirSync(path.join(root, 'build/temp/heart'), {recursive:true});
const source = execFileSync('c++', ['-E','-P','-x','c++','src/app/web_assets.h','-Itests','-Isrc/boards/jc4880p433-heart','-DBOARD_HAS_OVERRIDE','-DCONFIG_IDF_TARGET_ESP32P4','-DBOARD_HAS_PSRAM'],{encoding:'utf8',maxBuffer:8*1024*1024,stdio:['ignore','pipe','pipe']});
const assets = new Map();
for(const match of source.matchAll(/const uint8_t (\w+_gz)\[\] PROGMEM = \{([\s\S]*?)\};/g)) assets.set(match[1],zlib.gunzipSync(Buffer.from([...match[2].matchAll(/0x([0-9a-f]{2})/g)].map(value=>parseInt(value[1],16)))));
const capture = JSON.parse(fs.readFileSync(path.join(__dirname,'capture-d94c9109.json'),'utf8'));
const csv = fs.readFileSync(path.join(__dirname,'capture-d94c9109.csv'));
const entry = {id:capture.session.id,session_id:capture.session.id,started_unix_time_s:capture.session.started_unix_time_s,duration_s:capture.session.nominal_duration_s,sample_count:capture.session.sample_count,complete:true,sufficient_reliable_data:true,reliable_coverage:capture.summary.reliable_coverage,reliable_bpm_mean:capture.summary.reliable_bpm_mean};
let entries=[entry],corrupt=false,noBeats=false,delay=false,active=false;
const requests=[];
const server=http.createServer((request,response)=>{
  const url=new URL(request.url,'http://localhost').pathname;requests.push(request.method+' '+url);
  const send=(type,value,status=200)=>{response.writeHead(status,{'Content-Type':type});response.end(value);};
  const json=value=>send('application/json',JSON.stringify(value));
  const mappings={'/':['shell_html_gz','text/html'],'/portal.js':['portal_js_gz','text/javascript'],'/portal-all.css':['portal_all_css_gz','text/css'],'/api/section/heart-sessions':['heart_sessions_fragment_html_gz','text/html']};
  if(mappings[url]){const [name,type]=mappings[url];return send(type,assets.get(name));}
  if(url==='/api/portal/nav')return json({categories:[{id:'sensors',display_name:'Sensors',items:[{id:'heart-sessions',display_name:'Heart Sessions'}]}]});
  if(url==='/api/section/welcome')return send('text/html','<h2>Home</h2>');
  if(url==='/api/heart/sessions')return json({entries,import:{active,scanned:1,added:0,failed:0}});
  if(url==='/api/heart/sessions/import'){active=true;setTimeout(()=>active=false,400);return json({success:true});}
  if(url==='/api/heart/sessions/d94c9109'&&request.method==='DELETE'){entries=[];return json({success:true});}
  if(url==='/api/heart/sessions/d94c9109'){
    const data=noBeats?{...capture,beats:[]}:capture;
    if(delay)return setTimeout(()=>json(data),1000);
    return json(data);
  }
  if(url==='/api/heart/sessions/d94c9109/raw.csv')return send('text/csv',corrupt?Buffer.concat([csv,Buffer.from('\n')]):csv);
  if(url==='/api/info')return json({device_name:'Heart Monitor (Local Preview)',version:'1.40.0',board:'jc4880p433-heart',device_class:'heart_monitor',features:{},chip_model:'ESP32-P4',chip_revision:1,chip_cores:2,cpu_freq:360,flash_chip_size:16777216,psram_size:33554432,health_poll_interval_ms:60000});
  if(url==='/api/health')return json({cpu_usage:null,uptime_seconds:0,free_heap:0,free_psram:0});
  if(url.endsWith('.woff2')){const filename=path.join(root,'src/app/web',path.basename(url));if(fs.existsSync(filename))return send('font/woff2',fs.readFileSync(filename));}
  if(url.startsWith('/api/'))return json({});
  send('text/plain','Not found',404);
});
async function main(){
  await new Promise(resolve=>server.listen(process.argv.includes('--serve')?8874:0,'127.0.0.1',resolve));
  const url=`http://127.0.0.1:${server.address().port}/#heart-sessions`;
  if(process.argv.includes('--serve')){console.log(url);return;}
  const browser=await chromium.launch({headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.goto(url);await page.locator('[data-open]').waitFor();
    assert(!requests.some(request=>request.includes('/sessions/d94c9109')),'Listing fetched a full capture');
    await page.locator('[data-open]').click();
    await page.waitForFunction(()=>!document.querySelector('#report').hidden||document.querySelector('#status').textContent.includes('Unable'),undefined,{polling:100});
    assert.equal(await page.locator('#status').isVisible(),false,await page.locator('#status').textContent());
    assert.equal(await page.evaluate(()=>Object.keys(Chart.instances).length),8);
    assert.equal(await page.locator('#integrity').textContent().then(text=>text.includes('9bc0b2b4')),true);
    await page.screenshot({path:'build/temp/heart/portal-desktop.png',fullPage:true});
    await page.locator('#theme-toggle').click();await page.waitForFunction(()=>Chart.getChart('rate-chart').options.color==='#afbeb6',undefined,{polling:100});await page.screenshot({path:'build/temp/heart/portal-dark.png'});await page.locator('#theme-toggle').click();
    await page.locator('[data-view="variability"]').click();
    assert((await page.locator('#pair-status').textContent()).includes('108 of 108'));
    assert.equal(await page.evaluate(()=>Chart.getChart('poincare-chart').data.datasets[0].data.length),45);
    const pixels=await page.evaluate(()=>{const canvas=document.getElementById('poincare-chart'),data=canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;let count=0;for(let index=3;index<data.length;index+=4)if(data[index])count++;return count;});assert(pixels>1000);
    await page.locator('#replay-speed').selectOption('5');await page.locator('#replay-start').click();
    await page.waitForFunction(()=>Number(document.querySelector('#cursor').value)>.5,undefined,{polling:100});await page.locator('#replay-toggle').click();assert.equal(await page.locator('#replay-toggle').getAttribute('aria-pressed'),'false');
    await page.locator('#replay-reveal').uncheck();await page.locator('#poincare-chart').scrollIntoViewIfNeeded();
    const position=await page.evaluate(()=>{const chart=Chart.getChart('poincare-chart'),point=chart.data.datasets[0].data[0],box=chart.canvas.getBoundingClientRect();return {x:box.x+chart.scales.x.getPixelForValue(point.x),y:box.y+chart.scales.y.getPixelForValue(point.y)};});await page.mouse.click(position.x,position.y);
    await page.waitForFunction(()=>Number(document.querySelector('#cursor').value)>20,undefined,{polling:100});await page.locator('#pair-inspect').click();assert.equal(await page.locator('[data-view="signals"]').getAttribute('aria-selected'),'true');
    await page.locator('#signal-mode').selectOption('raw');assert.equal(await page.evaluate(()=>Chart.getChart('signal-chart').data.datasets.at(-1).yAxisID),'y');assert.equal(await page.evaluate(()=>Chart.getChart('signal-chart').options.scales.red.display),true);
    await page.locator('#full-range').click();await page.locator('#notes').fill('Fixture context <safe>');
    await page.locator('[data-view="variability"]').click();await page.screenshot({path:'build/temp/heart/portal-variability.png',fullPage:true});
    await page.evaluate(()=>window.dispatchEvent(new Event('beforeprint')));await page.pdf({path:'build/temp/heart/portal-report.pdf',printBackground:true});await page.evaluate(()=>window.dispatchEvent(new Event('afterprint')));
    const printed=execFileSync('pdftotext',['build/temp/heart/portal-report.pdf','-'],{encoding:'utf8'});assert(printed.includes('Time basis & integrity'));assert(printed.includes('heart_reliable_v1'));
    assert.equal(await page.locator('[data-view="variability"]').getAttribute('aria-selected'),'true');
    for(const width of [390,320]){
      await page.setViewportSize({width,height:900});
      await page.waitForFunction(()=>{const canvas=document.getElementById('poincare-chart'),box=canvas.getBoundingClientRect();return Math.abs(box.width-box.height)<2;},undefined,{polling:100});
      assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'Mobile overflow at '+width);
      await page.screenshot({path:`build/temp/heart/portal-mobile-${width}.png`,fullPage:true});
    }
    await page.locator('#heart-session-back').click();assert.equal(await page.evaluate(()=>Object.keys(Chart.instances).length),0);
    delay=true;await page.locator('[data-open]').click();await page.locator('#heart-session-back').click();assert.equal(await page.evaluate(()=>Object.keys(Chart.instances).length),0);delay=false;
    await page.locator('[data-open]').click();await page.locator('#report').waitFor({state:'visible'});assert.equal(await page.locator('#notes').inputValue(),'Fixture context <safe>');assert.equal(await page.locator('#signal-mode').inputValue(),'filtered');assert.equal(await page.locator('#replay-reveal').isChecked(),false);
    await page.locator('#heart-session-back').click();corrupt=true;await page.locator('[data-open]').click();await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('checksum'),undefined,{polling:100});assert.equal(await page.evaluate(()=>Object.keys(Chart.instances).length),0);await page.locator('#heart-session-back').click();corrupt=false;
    noBeats=true;await page.locator('[data-open]').click();await page.locator('#report').waitFor({state:'visible'});await page.locator('[data-view="variability"]').click();assert((await page.locator('#pair-status').textContent()).includes('0 of 0'));await page.evaluate(()=>{location.hash='welcome';});await page.locator('#content-pane h2').waitFor();assert.equal(await page.evaluate(()=>Object.keys(Chart.instances).length),0);noBeats=false;
    await page.evaluate(()=>{location.hash='heart-sessions';});await page.locator('[data-open]').waitFor();await page.locator('#heart-session-import').click();await page.waitForFunction(()=>!document.querySelector('#heart-session-import').disabled,undefined,{polling:100});
    page.once('dialog',dialog=>dialog.accept());await page.locator('[data-delete]').click();await page.waitForFunction(()=>document.querySelector('#heart-session-list').textContent.includes('No matching'),undefined,{polling:100});
    assert.deepEqual(errors,[]);console.log('PASS: production minified shell, catalog-only list, paired capture, charts, replay, pointer selection, axes, print, mobile, abort/cleanup, notes, checksum errors, empty beats, import and delete');
  } finally {await browser.close();await new Promise(resolve=>server.close(resolve));}
}
main().catch(error=>{console.error(error);server.close();process.exitCode=1;});