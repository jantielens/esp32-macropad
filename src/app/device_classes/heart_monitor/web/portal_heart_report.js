window.heartSessionReport = (function () {
  'use strict';
  function mount(root, sessionId) {
    const element = id => root.querySelector('#' + id);
    const escape = value => String(value ?? '').replace(/[&<>"']/g, character => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[character]));
    const number = (value, digits = 1) => Number.isFinite(value) ? value.toFixed(digits) : 'Unavailable';
    const label = value => String(value ?? '').replace(/_/g, ' ');
    const colors = {ir:'#087f72',red:'#c84a60',candidate:'#939c99'};
    const charts = {}, controller = new AbortController();
    function updateTheme() {
      const style=getComputedStyle(root),color=style.getPropertyValue('--heart-muted').trim(),grid=style.getPropertyValue('--heart-line').trim();
      for(const chart of Object.values(charts)) {
        chart.options.color=color;
        for(const scale of Object.values(chart.options.scales)){scale.ticks.color=color;scale.title.color=color;scale.grid.color=grid+'77';}
        chart.update('none');
      }
    }
    const themeObserver=new MutationObserver(updateTheme);
    themeObserver.observe(document.documentElement,{attributes:true,attributeFilter:['data-bs-theme']});
    let report, raw, rate, duration, cursor = 0, range = [0,0], view = 'overview', disposed = false;
    let intervals = [], pairs = [], selectedPair = null, replayFrame = null, replayLast = null, printReveal = false;
    const seconds = index => index / rate;
    const timeChart = id => !['distribution-chart','pulse-distribution-chart','poincare-chart'].includes(id);
    const readingAt = time => report.readings.findLast(reading => seconds(reading.sample_index) <= time) || report.readings[0];
    const flags = mask => report.estimator.quality_flag_definitions.filter(flag => (mask & flag.mask) !== 0).map(flag => label(flag.name)).join(', ') || 'None';
    const periods = () => report.events.filter(event => event.type === 'quality_change').map((event,index,events) => ({...event,start:seconds(event.sample_index),end:index+1<events.length?seconds(events[index+1].sample_index):duration}));
    const listen = (target,event,handler) => target.addEventListener(event,handler,{signal:controller.signal});
    const line = (name,data,color,extra={}) => ({label:name,data,borderColor:color,backgroundColor:color,borderWidth:1.6,pointRadius:0,spanGaps:false,...extra});
    const fields = (id,entries) => {element(id).innerHTML=entries.map(([key,value])=>`<dt>${escape(key)}</dt><dd>${escape(value)}</dd>`).join('');};
    const cell = (name,value) => `<div><strong>${escape(value)}</strong>${escape(name)}</div>`;
    const table = (headers,rows) => `<table><thead><tr>${headers.map(header=>`<th>${escape(header)}</th>`).join('')}</tr></thead><tbody>${rows.map(row=>`<tr>${row.map(value=>`<td>${value}</td>`).join('')}</tr>`).join('')}</tbody></table>`;
    function scaleBounds(values) {
      const sorted=values.filter(Number.isFinite).sort((left,right)=>left-right);
      if(!sorted.length)return {};
      const typical=element('scale-mode').value==='typical'&&sorted.length>=20;
      const low=sorted[Math.floor((sorted.length-1)*(typical?.05:0))],high=sorted[Math.ceil((sorted.length-1)*(typical?.95:1))];
      const padding=Math.max((high-low)*.1,Math.abs(high)*.00001,.1);
      return {min:low-padding,max:high+padding};
    }
    const guide = {
      id:'heartSessionGuide',
      beforeDatasetsDraw(chart) {
        if(chart.canvas.id!=='rate-chart'||!chart.chartArea)return;
        const {ctx,chartArea,scales}=chart;ctx.save();
        for(const period of periods()) {
          if(!period.reliable&&period.state!=='measuring')continue;
          const left=Math.max(chartArea.left,scales.x.getPixelForValue(period.start)),right=Math.min(chartArea.right,scales.x.getPixelForValue(period.end));
          if(right<=left)continue;
          ctx.fillStyle=period.reliable?'#087f7210':'#64716c18';ctx.fillRect(left,chartArea.top,right-left,chartArea.bottom-chartArea.top);
        }
        ctx.restore();
      },
      afterDatasetsDraw(chart) {
        if(!timeChart(chart.canvas.id)||!chart.chartArea||cursor<chart.scales.x.min||cursor>chart.scales.x.max)return;
        const {ctx,chartArea}=chart,position=chart.scales.x.getPixelForValue(cursor);
        ctx.save();ctx.strokeStyle='#25332f88';ctx.setLineDash([4,4]);ctx.beginPath();ctx.moveTo(position,chartArea.top);ctx.lineTo(position,chartArea.bottom);ctx.stroke();ctx.restore();
      }
    };
    function makeChart(id,datasets,title,extra={}) {
      charts[id]=new Chart(element(id),{type:'line',data:{datasets},plugins:[guide],options:{responsive:true,maintainAspectRatio:false,animation:false,parsing:false,normalized:true,interaction:{mode:'nearest',axis:'x',intersect:false},plugins:{legend:{display:false},tooltip:{callbacks:{title:items=>`${number(items[0]?.parsed.x,2)} s`}}},scales:{x:{type:'linear',min:range[0],max:range[1],title:{display:true,text:'Nominal time (s)'},grid:{color:'#d8e1dc55'}},y:{title:{display:true,text:title},grid:{color:'#d8e1dc77'},...extra}},onClick:(event,items,chart)=>{pauseReplay();setCursor(chart.scales.x.getValueForPixel(event.x));},onHover:(event,items,chart)=>{if(replayFrame===null&&event.x>=chart.chartArea.left&&event.x<=chart.chartArea.right&&event.y>=chart.chartArea.top&&event.y<=chart.chartArea.bottom)setCursor(chart.scales.x.getValueForPixel(event.x));}}});
    }
    function readingSeries(key,predicate=()=>true) {
      return report.readings.flatMap((reading,index,readings)=>{
        const value=predicate(reading)&&Number.isFinite(reading[key])?reading[key]:null,end=index+1<readings.length?seconds(readings[index+1].sample_index):duration;
        return [{x:seconds(reading.sample_index),y:value},{x:end,y:value}];
      });
    }
    function setCursor(time) {
      if(disposed||!Number.isFinite(time))return;
      cursor=Math.max(range[0],Math.min(range[1],time));element('cursor').value=cursor;element('cursor-time').textContent=`${number(cursor,2)} s`;
      const reading=readingAt(cursor);
      element('inspector').innerHTML=reading?cell('Nominal time',`${number(cursor,2)} s`)+cell('Recorded BPM',reading.pulse_valid?number(reading.bpm):'No valid pulse')+cell('Reliability',reading.reliable?'Reliable':'Not reliable')+cell('Method',label(reading.method))+cell('Red-channel BPM',number(reading.red_bpm))+cell('Correlation',number(reading.correlation,3))+cell('Period distinctness',number(reading.period_distinctness,3))+cell('Peak dispersion',number(reading.interval_dispersion,3))+cell('IR RMS',`${number(reading.rms_counts)} counts`)+`<div class="flags">Flags: ${escape(flags(reading.quality_flags))} · Reading at ${number(seconds(reading.sample_index),2)} s${Number.isFinite(reading.window_start_index)?` · Estimator window ${number(seconds(reading.window_start_index),2)}-${number(seconds(reading.window_end_index_exclusive),2)} s`:''}</div>`:'No recorded BPM readings';
      const beat=report.beats.reduce((nearest,current)=>!nearest||Math.abs(seconds(current.sample_index)-cursor)<Math.abs(seconds(nearest.sample_index)-cursor)?current:nearest,null);
      element('beat-detail').innerHTML=beat?cell('Nearest detected peak',`${number(seconds(beat.sample_index),2)} s`)+cell('Interval',Number.isFinite(beat.ibi_ms)?`${number(beat.ibi_ms,0)} ms`:'No previous peak')+cell('Interval check',beat.interval_valid?'Within detector bounds':label(beat.interval_rejection_reason||'Rejected'))+cell('BPM at detection',beat.bpm_reliable_at_detection?'Reliable':'Not reliable'):'No detected peaks';
      updatePulseCursor();for(const [id,chart] of Object.entries(charts))if(timeChart(id))chart.draw();
    }
    function updateScales() {
      for(const id of ['rate-chart','interval-chart','quality-chart','pulse-timeline-chart']) {
        const chart=charts[id],values=chart.data.datasets.flatMap(dataset=>dataset.data.filter(point=>point.x>=range[0]&&point.x<=range[1]).map(point=>point.y)),bounds=scaleBounds(values);
        if(id==='quality-chart'){bounds.min=0;bounds.max=Math.max(bounds.max||1,report.estimator.reliability_policy.min_correlation,report.estimator.reliability_policy.min_period_distinctness);}
        delete chart.options.scales.y.min;delete chart.options.scales.y.max;Object.assign(chart.options.scales.y,bounds);chart.update('none');
      }
    }
    function setRange(start,end) {
      if(!Number.isFinite(start)||!Number.isFinite(end)||start<0||end>duration||end<=start){element('range-start').setCustomValidity('Choose a range within the session, with end after start.');element('range-start').reportValidity();return;}
      element('range-start').setCustomValidity('');pauseReplay();range=[start,end];selectedPair=null;
      element('range-start').value=number(start,2);element('range-end').value=number(end,2);element('range-label').textContent=`${number(start,1)}-${number(end,1)} s`;element('cursor').min=start;element('cursor').max=end;
      for(const [id,chart] of Object.entries(charts))if(timeChart(id)){chart.options.scales.x.min=start;chart.options.scales.x.max=end;}
      updateSignals();updateVariability();updateScales();setCursor(cursor);
    }
    function showView(next) {
      if(next!=='variability')pauseReplay();view=next;
      root.querySelectorAll('[data-panel]').forEach(panel=>panel.hidden=panel.dataset.panel!==view);
      root.querySelectorAll('[data-view]').forEach(button=>button.setAttribute('aria-selected',String(button.dataset.view===view)));
      for(const chart of Object.values(charts))chart.resize();updateSignals();
    }
    function updateSignals() {
      const filtered=element('signal-mode').value==='filtered',start=Math.max(0,Math.floor(range[0]*rate)),end=Math.min(raw.length,Math.ceil(range[1]*rate)+1),stride=Math.max(1,Math.floor((end-start)/2400));
      const valueAt=(sample,key)=>key==='filtered'?report.waveform.filtered_ir_counts[(sample.index-report.waveform.start_sample_index)/report.waveform.step_samples]??null:sample[key];
      const selectPoints=key=>{
        const points=[];
        for(let index=start;index<end;index+=stride){
          const values=raw.slice(index,Math.min(end,index+stride)).map(sample=>({x:seconds(sample.index),y:valueAt(sample,key)}));
          if(stride===1||values.some(point=>point.y===null)){points.push(values[0]);if(values.some(point=>point.y===null))points.push({x:seconds(index),y:null});continue;}
          const minimum=values.reduce((best,point)=>point.y<best.y?point:best),maximum=values.reduce((best,point)=>point.y>best.y?point:best);points.push(...[minimum,maximum].sort((left,right)=>left.x-right.x));
        }
        return points;
      };
      const datasets=filtered?[line('Filtered IR',selectPoints('filtered'),colors.ir,{yAxisID:'y'})]:[line('IR',selectPoints('ir'),colors.ir,{yAxisID:'y'}),line('Red',selectPoints('red'),colors.red,{yAxisID:'red'})];
      if(element('show-beats').checked)datasets.push(line('Detected peak',report.beats.filter(beat=>seconds(beat.sample_index)>=range[0]&&seconds(beat.sample_index)<=range[1]&&raw[beat.sample_index]).map(beat=>({x:seconds(beat.sample_index),y:valueAt(raw[beat.sample_index],filtered?'filtered':'ir')})),colors.red,{yAxisID:'y',showLine:false,pointRadius:3,pointStyle:'triangle'}));
      const irValues=raw.slice(start,end).map(sample=>valueAt(sample,filtered?'filtered':'ir')),redValues=filtered?[]:raw.slice(start,end).map(sample=>sample.red),irBounds=scaleBounds(irValues),redBounds=scaleBounds(redValues),chart=charts['signal-chart'];
      chart.options.scales.y={type:'linear',position:'left',title:{display:true,text:filtered?'Filtered IR (counts)':'IR (counts)',color:colors.ir},ticks:{color:colors.ir},grid:{color:'#d8e1dc77'},...irBounds};
      chart.options.scales.red={type:'linear',position:'right',display:!filtered,title:{display:true,text:'Red (counts)',color:colors.red},ticks:{color:colors.red},grid:{drawOnChartArea:false},...redBounds};
      const outside=(values,bounds)=>values.filter(value=>Number.isFinite(value)&&(value<bounds.min||value>bounds.max)).length;
      element('scale-status').textContent=element('scale-mode').value==='typical'?`Typical range with padding: ${outside(irValues,irBounds)} IR${filtered?'':` / ${outside(redValues,redBounds)} red`} samples outside the visible scale. All samples remain in the capture.`:'Full sample range with padding. No extremes excluded.';
      chart.data.datasets=datasets;chart.update('none');
    }
    function buildIntervals() {
      const missing=[0];for(const value of report.waveform.filtered_ir_counts)missing.push(missing.at(-1)+(Number.isFinite(value)?0:1));
      intervals=report.beats.slice(1).map((beat,index)=>{
        const previous=report.beats[index],first=Math.floor((previous.sample_index-report.waveform.start_sample_index)/report.waveform.step_samples),last=Math.ceil((beat.sample_index-report.waveform.start_sample_index)/report.waveform.step_samples);
        const continuous=beat.beat_count===previous.beat_count+1&&beat.sample_index>previous.sample_index&&first>=0&&last<missing.length-1&&missing[last+1]===missing[first]&&Number.isFinite(beat.ibi_ms)&&Math.abs(beat.ibi_ms-(beat.sample_index-previous.sample_index)*1000/rate)<1000/rate;
        return {index:index+1,start:seconds(previous.sample_index),time:seconds(beat.sample_index),value:beat.ibi_ms,bounded:continuous&&beat.interval_valid};
      });
      pairs=intervals.slice(1).flatMap((current,index)=>{
        const previous=intervals[index];if(!previous.bounded||!current.bounded)return [];
        return [{x:previous.value,y:current.value,start:previous.start,time:current.time,reliable:report.beats.slice(current.index-2,current.index+1).every(beat=>beat.bpm_reliable_at_detection)}];
      });
    }
    const inRange=interval=>interval.start>=range[0]&&interval.time<=range[1];
    function showPair(pair) {
      selectedPair=pair||null;element('pair-inspect').disabled=!pair;
      element('pair-detail').textContent=pair?`${number(pair.start,2)}-${number(pair.time,2)} s · ${number(pair.x,0)} -> ${number(pair.y,0)} ms · Change ${number(pair.y-pair.x,0)} ms · ${pair.reliable?'Reliable BPM at endpoints':'BPM not reliable at every endpoint'}`:'No adjacent detector-bounded pair in this range.';
      charts['poincare-chart'].data.datasets[3].data=pair?[pair]:[];charts['poincare-chart'].update('none');
    }
    function updatePulseCursor() {
      if(!charts['poincare-chart'])return;
      element('replay-time').textContent=`${number(cursor,2)} s`;
      const all=pairs.filter(inRange),revealing=element('replay-reveal').checked,visible=all.filter(pair=>!revealing||pair.time<=cursor),chart=charts['poincare-chart'];
      chart.data.datasets[0].data=visible.filter(pair=>pair.reliable);chart.data.datasets[1].data=visible.filter(pair=>!pair.reliable);
      const outside=visible.filter(pair=>pair.x<chart.options.scales.x.min||pair.x>chart.options.scales.x.max||pair.y<chart.options.scales.y.min||pair.y>chart.options.scales.y.max).length;
      element('pair-status').textContent=`${visible.length} of ${all.length} adjacent pairs${revealing?' revealed':''}; ${outside} outside the visible scale. Equal interval axes; diagonal = unchanged interval. All values restores extremes.`;
      showPair(visible.reduce((best,pair)=>!best||Math.abs(pair.time-cursor)<Math.abs(best.time-cursor)?pair:best,null));
      const available=intervals.filter(interval=>Number.isFinite(interval.value)&&(!revealing||interval.time<=cursor)),timeline=charts['pulse-timeline-chart'];
      timeline.data.datasets[0].data=available.filter(interval=>interval.bounded).map(interval=>({x:interval.time,y:interval.value}));timeline.data.datasets[1].data=available.filter(interval=>!interval.bounded).map(interval=>({x:interval.time,y:interval.value}));timeline.update('none');
      const bins=new Map();for(const interval of available.filter(inRange)){const bin=Math.floor(interval.value/100)*100;if(!bins.has(bin))bins.set(bin,[0,0]);bins.get(bin)[interval.bounded?0:1]++;}
      const ordered=[...bins.keys()].sort((left,right)=>left-right),histogram=charts['pulse-distribution-chart'];histogram.data.labels=ordered.map(bin=>`${bin}-${bin+100}`);histogram.data.datasets.forEach((dataset,index)=>dataset.data=ordered.map(bin=>bins.get(bin)[index]));histogram.update('none');
    }
    function updateVariability() {
      const selected=intervals.filter(inRange),bounded=selected.filter(interval=>interval.bounded),selectedPairs=pairs.filter(inRange);
      element('variability-summary').innerHTML=[['Interval records',selected.length],['Detector-bounded intervals',bounded.length],['Rejected / interrupted',selected.length-bounded.length],['Adjacent pairs',selectedPairs.length]].map(([name,value])=>cell(name,value)).join('');
      let longest=0,start=null;for(const interval of selected){if(interval.bounded){if(start===null)start=interval.start;longest=Math.max(longest,interval.time-start);}else start=null;}
      fields('variability-facts',[['Scope','Fully contained intervals in selected range'],['Timing step',`${number(1000/rate,0)} ms`],['Unavailable interval durations',selected.filter(interval=>!Number.isFinite(interval.value)).length],['Longest detector-bounded run',`${number(longest,2)} s`],['Pairs with reliable BPM at all endpoints',selectedPairs.filter(pair=>pair.reliable).length],['Validated normal intervals','Not established'],['Artifact correction','None; recorded intervals retained']]);
      const bounds=scaleBounds(selectedPairs.flatMap(pair=>[pair.x,pair.y])),minimum=bounds.min??0,maximum=bounds.max??2000,chart=charts['poincare-chart'];Object.assign(chart.options.scales.x,{min:minimum,max:maximum});Object.assign(chart.options.scales.y,{min:minimum,max:maximum});chart.data.datasets[2].data=[{x:minimum,y:minimum},{x:maximum,y:maximum}];updatePulseCursor();
    }
    function pauseReplay() {
      if(replayFrame!==null)cancelAnimationFrame(replayFrame);replayFrame=null;replayLast=null;
      element('replay-toggle').innerHTML='<span class="portal-icon" aria-hidden="true">play_arrow</span>';element('replay-toggle').setAttribute('aria-pressed','false');element('replay-toggle').title='Play recorded session';element('replay-toggle').setAttribute('aria-label','Play recorded session');
    }
    function playReplay() {
      if(cursor>=range[1])setCursor(range[0]);element('replay-reveal').checked=true;
      element('replay-toggle').innerHTML='<span class="portal-icon" aria-hidden="true">pause</span>';element('replay-toggle').setAttribute('aria-pressed','true');element('replay-toggle').title='Pause recorded session';element('replay-toggle').setAttribute('aria-label','Pause recorded session');updatePulseCursor();
      const tick=now=>{if(disposed)return;const delta=replayLast===null?0:(now-replayLast)/1000;replayLast=now;setCursor(cursor+delta*Number(element('replay-speed').value));if(cursor>=range[1])pauseReplay();else replayFrame=requestAnimationFrame(tick);};replayFrame=requestAnimationFrame(tick);
    }
    function renderEvents() {
      const filter=element('event-filter').value,events=report.events.filter(event=>filter==='all'||event.type==='quality_change'&&Boolean(event.reliable)===(filter==='reliable'));
      element('events').innerHTML=table(['Time','Event','Status','Quality flags'],events.map(event=>[`<button data-time="${seconds(event.sample_index)}">${number(seconds(event.sample_index),2)} s</button>`,escape(label(event.type)),event.type==='quality_change'?escape(event.reliable?'Reliable':label(event.state)):'',event.quality_flags!==undefined?escape(flags(event.quality_flags)):'']));
    }
    function initializeCharts() {
      makeChart('rate-chart',[line('Reliable BPM',readingSeries('bpm',reading=>reading.reliable),colors.ir,{borderWidth:2.2}),line('Other pulse estimates',readingSeries('bpm',reading=>!reading.reliable&&reading.pulse_valid),colors.candidate,{borderDash:[4,4]}),line('Candidate BPM',readingSeries('candidate_bpm',reading=>!reading.pulse_valid),colors.candidate,{borderDash:[2,5]})],'BPM');makeChart('signal-chart',[],'Filtered IR (counts)');
      makeChart('interval-chart',[line('In-range intervals',report.beats.filter(beat=>beat.interval_valid).map(beat=>({x:seconds(beat.sample_index),y:beat.ibi_ms})),colors.ir,{showLine:false,pointRadius:3}),line('Rejected intervals',report.beats.filter(beat=>!beat.interval_valid&&Number.isFinite(beat.ibi_ms)).map(beat=>({x:seconds(beat.sample_index),y:beat.ibi_ms})),colors.red,{showLine:false,pointRadius:4,pointStyle:'crossRot'})],'Interval (ms)');
      makeChart('quality-chart',[line('Correlation',readingSeries('correlation'),colors.ir),line('Period distinctness',readingSeries('period_distinctness'),colors.red),line('Peak interval dispersion',readingSeries('interval_dispersion'),colors.candidate),line('Minimum correlation',[{x:0,y:report.estimator.reliability_policy.min_correlation},{x:duration,y:report.estimator.reliability_policy.min_correlation}],colors.ir,{borderDash:[5,5]}),line('Minimum distinctness',[{x:0,y:report.estimator.reliability_policy.min_period_distinctness},{x:duration,y:report.estimator.reliability_policy.min_period_distinctness}],colors.red,{borderDash:[5,5]})],'Score',{min:0});
      const bins=new Map();report.readings.forEach((reading,index,readings)=>{if(!reading.reliable||!Number.isFinite(reading.bpm))return;const bin=Math.floor(reading.bpm/2)*2,end=index+1<readings.length?seconds(readings[index+1].sample_index):duration;bins.set(bin,(bins.get(bin)||0)+end-seconds(reading.sample_index));});
      const ordered=[...bins.keys()].sort((left,right)=>left-right);
      charts['distribution-chart']=new Chart(element('distribution-chart'),{type:'bar',data:{labels:ordered.map(bin=>`${bin}-${bin+2}`),datasets:[{label:'Reliable duration (s)',data:ordered.map(bin=>bins.get(bin)),backgroundColor:colors.ir}]},options:{responsive:true,maintainAspectRatio:false,animation:false,plugins:{legend:{display:false}},scales:{x:{title:{display:true,text:'Reliable BPM (2 BPM bins)'}},y:{title:{display:true,text:'Nominal duration (s)'},beginAtZero:true}}}});
      buildIntervals();makeChart('pulse-timeline-chart',[line('Within detector bounds',[],colors.ir,{showLine:false,pointRadius:3}),line('Rejected / interrupted',[],colors.red,{showLine:false,pointRadius:4,pointStyle:'crossRot'})],'Interval (ms)');
      charts['poincare-chart']=new Chart(element('poincare-chart'),{type:'scatter',data:{datasets:[line('Reliable BPM at endpoints',[],colors.ir,{showLine:false,pointRadius:4}),line('Other detector-bounded pairs',[],colors.candidate,{showLine:false,pointRadius:4}),line('Unchanged interval',[],colors.candidate,{showLine:true,borderDash:[4,4]}),line('Cursor pair',[],colors.red,{showLine:false,pointRadius:7,backgroundColor:'#c84a6033',pointBorderColor:colors.red,pointBorderWidth:2})]},options:{responsive:true,maintainAspectRatio:false,animation:false,parsing:false,interaction:{mode:'nearest',intersect:true},plugins:{legend:{display:false},tooltip:{filter:item=>item.datasetIndex!==2,callbacks:{label:item=>`${number(item.raw.x,0)} -> ${number(item.raw.y,0)} ms · ${number(item.raw.time,2)} s`}}},scales:{x:{type:'linear',title:{display:true,text:'Previous interval (ms)'}},y:{type:'linear',title:{display:true,text:'Next interval (ms)'}}},onClick:(event,items,chart)=>{const item=items.find(item=>item.datasetIndex!==2);if(!item)return;pauseReplay();const pair=chart.data.datasets[item.datasetIndex].data[item.index];setCursor(pair.time);showPair(pair);}}});
      charts['pulse-distribution-chart']=new Chart(element('pulse-distribution-chart'),{type:'bar',data:{labels:[],datasets:[{label:'Within detector bounds',data:[],backgroundColor:colors.ir},{label:'Rejected / interrupted',data:[],backgroundColor:colors.red}]},options:{responsive:true,maintainAspectRatio:false,animation:false,plugins:{legend:{display:false}},scales:{x:{stacked:true,title:{display:true,text:'Interval (ms, 100 ms bins)'}},y:{stacked:true,beginAtZero:true,ticks:{precision:0},title:{display:true,text:'Recorded interval count'}}}}});
    }
    function connectControls() {
      root.querySelectorAll('[data-view]').forEach(button=>listen(button,'click',()=>showView(button.dataset.view)));
      listen(root,'click',event=>{const button=event.target.closest('[data-time],[data-start]');if(!button||!root.contains(button))return;pauseReplay();if(button.dataset.start!==undefined){setRange(Number(button.dataset.start),Number(button.dataset.end));if(!button.dataset.keepView)showView('signals');setCursor(Number(button.dataset.start));}else{const time=Number(button.dataset.time);if(time<range[0]||time>range[1])setRange(0,duration);setCursor(time);element('inspector').scrollIntoView({block:'nearest',behavior:'smooth'});}});
      listen(element('cursor'),'input',event=>{pauseReplay();setCursor(Number(event.target.value));});listen(element('apply-range'),'click',()=>setRange(Number(element('range-start').value),Number(element('range-end').value)));listen(element('full-range'),'click',()=>setRange(0,duration));listen(element('inspect-range'),'click',()=>{const start=Math.max(0,Math.min(Math.max(0,duration-10),cursor-5));setRange(start,Math.min(duration,start+10));showView('signals');});
      listen(element('signal-mode'),'change',updateSignals);listen(element('show-beats'),'change',updateSignals);listen(element('event-filter'),'change',renderEvents);listen(element('scale-mode'),'change',()=>{updateSignals();updateVariability();updateScales();});
      listen(element('replay-toggle'),'click',()=>replayFrame===null?playReplay():pauseReplay());listen(element('replay-start'),'click',()=>{pauseReplay();setCursor(range[0]);playReplay();});listen(element('replay-reveal'),'change',()=>{pauseReplay();updatePulseCursor();updateScales();});listen(element('pair-inspect'),'click',()=>{if(!selectedPair)return;const pair=selectedPair;setRange(Math.max(0,pair.start-.5),Math.min(duration,pair.time+.5));showView('signals');setCursor(pair.time);});listen(document,'visibilitychange',()=>{if(document.hidden)pauseReplay();});
      const notesKey=`heart-report-notes-${sessionId}`;try{element('notes').value=localStorage.getItem(notesKey)||'';}catch{element('notes-state').textContent='Browser storage unavailable';}
      listen(element('notes'),'input',()=>{try{localStorage.setItem(notesKey,element('notes').value);element('notes-state').textContent='Saved in this browser only';}catch{element('notes-state').textContent='Could not save notes';}});
      listen(window,'beforeprint',()=>{pauseReplay();document.body.classList.add('heart-session-print');printReveal=element('replay-reveal').checked;element('replay-reveal').checked=false;updatePulseCursor();root.querySelectorAll('[data-panel]').forEach(panel=>panel.hidden=false);for(const chart of Object.values(charts))chart.resize();updateTheme();});
      listen(window,'afterprint',()=>{document.body.classList.remove('heart-session-print');element('replay-reveal').checked=printReveal;showView(view);updatePulseCursor();updateTheme();});
    }
    async function initialize() {
      element('status').hidden=false;element('status').textContent='Loading session report...';element('report').hidden=true;
      if(!/^[0-9a-f]{8}$/.test(sessionId))throw Error('Invalid session ID');
      root.querySelectorAll('[data-panel]').forEach(panel=>panel.hidden=panel.dataset.panel!=='overview');root.querySelectorAll('[data-view]').forEach(button=>button.setAttribute('aria-selected',String(button.dataset.view==='overview')));
      element('replay-reveal').checked=false;element('signal-mode').value='filtered';element('show-beats').checked=true;element('scale-mode').value='typical';element('event-filter').value='all';element('notes-state').textContent='Stored in this browser only';
      const url=`/api/heart/sessions/${sessionId}`,response=await fetch(url,{signal:controller.signal});if(!response.ok)throw Error(`Session report: HTTP ${response.status}`);report=await response.json();
      if(report.format!=='heart_session_v1'||report.schema_version!==1||report.export_complete!==true||report.session.id!==sessionId||report.session.raw_csv!==`capture-${sessionId}.csv`)throw Error('Unsupported, mismatched or incomplete session report');
      rate=report.acquisition.sample_rate_hz;duration=report.session.nominal_duration_s;
      if(!(rate>0)||!Number.isFinite(duration)||duration<0||!Number.isInteger(report.session.sample_count)||report.session.sample_count<0||report.session.sample_count>30000||!Array.isArray(report.readings)||!Array.isArray(report.beats)||!Array.isArray(report.events))throw Error('Invalid session dimensions');
      const csvUrl=url+'/raw.csv';element('status').textContent='Loading and checking paired raw capture...';const csvResponse=await fetch(csvUrl,{signal:controller.signal});if(!csvResponse.ok)throw Error(`Raw capture: HTTP ${csvResponse.status}`);const csvText=await csvResponse.text();
      const rows=csvText.trim().split(/\r?\n/).filter(row=>row&&!row.startsWith('#'));if(rows.shift()!=='sample_index,nominal_time_us,read_elapsed_us,red,ir')throw Error('Unexpected raw CSV columns');
      raw=rows.map(row=>{const values=row.split(',').map(Number);if(values.length!==5||!values.every(Number.isFinite))throw Error('Malformed raw sample');return {index:values[0],nominal:values[1],elapsed:values[2],red:values[3],ir:values[4]};});
      if(raw.length!==report.session.sample_count||raw.some((sample,index)=>sample.index!==index)||report.waveform.filtered_ir_counts.length!==raw.length||report.waveform.start_sample_index!==0||report.waveform.step_samples!==1)throw Error('Raw sample / report alignment mismatch');
      const bytes=new TextEncoder().encode(csvText);let checksum=0xffffffff;for(const byte of bytes){checksum^=byte;for(let bit=0;bit<8;bit++)checksum=(checksum>>>1)^((checksum&1)?0xedb88320:0);}const crc=((checksum^0xffffffff)>>>0).toString(16).padStart(8,'0');
      if(bytes.length!==report.session.raw_csv_bytes||crc!==report.session.raw_csv_checksum.value.toLowerCase())throw Error('Raw CSV size or checksum does not match the report');
      if(disposed)return;if(!raw.length||duration===0){element('status').textContent='This capture has no stored samples. JSON and CSV remain available from the session list.';return;}
      range=[0,duration];const summary=report.summary;
      element('session-title').textContent=`Session ${sessionId}`;element('session-meta').textContent=`${Number.isFinite(report.session.started_unix_time_s)&&report.session.started_unix_time_s>0?new Date(report.session.started_unix_time_s*1000).toLocaleString():'Clock unavailable'} · ${number(duration,2)} s · ${report.session.board}`;element('json-download').href=url;element('json-download').download=`capture-${sessionId}.json`;element('csv-download').href=csvUrl;element('csv-download').download=`capture-${sessionId}.csv`;
      element('verdict').textContent=`${number(summary.reliable_coverage*100)}% reliable coverage (${number(summary.reliable_duration_s,2)} s of ${number(duration,2)} s). ${summary.sufficient_reliable_data?'Summary statistics describe reliable periods only.':'Insufficient reliable data for a session BPM summary.'} Other estimates remain available for inspection.`;
      element('metrics').innerHTML=[['Reliable mean',summary.sufficient_reliable_data?number(summary.reliable_bpm_mean):'Unavailable','BPM','Nominal-duration weighted'],['Reliable range',summary.sufficient_reliable_data?`${number(summary.reliable_bpm_min)}-${number(summary.reliable_bpm_max)}`:'Unavailable','BPM','Reliable periods only'],['Reliable coverage',number(summary.reliable_coverage*100),'%','Not whole-session confidence'],['First usable reading',number(summary.time_to_first_reading_s,2),'s','From recording start'],['Detected peaks',report.beats.length,'','Not verified heartbeats']].map(([name,value,unit,description])=>`<div class="metric"><div>${escape(name)}</div><strong>${escape(value)}<span class="unit">${escape(unit)}</span></strong><small>${escape(description)}</small></div>`).join('');
      const segments=[];for(const period of periods())if(period.reliable){const previous=segments.at(-1);if(previous&&previous.end===period.start)previous.end=period.end;else segments.push({start:period.start,end:period.end});}
      element('coverage-strip').innerHTML=periods().map(period=>`<button style="width:${(period.end-period.start)/duration*100}%;background:${period.reliable?colors.ir:period.state==='weak_signal'?'#fce9c7':'#e7ebef'}" data-start="${period.start}" data-end="${period.end}" data-keep-view="true" title="${escape(period.reliable?'Reliable':label(period.state))}: ${number(period.start,2)}-${number(period.end,2)} s"></button>`).join('');element('segments').innerHTML=table(['Period','Start','End','Duration',''],segments.map((segment,index)=>[index+1,`${number(segment.start,2)} s`,`${number(segment.end,2)} s`,`${number(segment.end-segment.start,2)} s`,`<button data-start="${segment.start}" data-end="${segment.end}">Inspect</button>`]));
      fields('glance',[['Reliable median',summary.sufficient_reliable_data?`${number(summary.reliable_bpm_median)} BPM`:'Unavailable'],['Usable signal',`${number(summary.usable_duration_s,2)} s (${number(summary.usable_coverage*100)}%)`],['Longest reliable period',`${number(summary.longest_reliable_segment_s,2)} s`],['Stored samples',raw.length.toLocaleString()],['Lost samples',report.acquisition.lost_samples],['FIFO overflow counter',report.acquisition.fifo_overflow_counter],['Capture status',report.session.complete?'Complete':'Interrupted']]);
      fields('acquisition',Object.entries(report.acquisition).map(([key,value])=>[label(key),Array.isArray(value)?value.join(', ')||'None':String(value)]));fields('policy',[...Object.entries(report.estimator).filter(([key,value])=>!['reliability_policy','quality_flag_definitions'].includes(key)&&typeof value!=='object').map(([key,value])=>[label(key),String(value)]),...Object.entries(report.estimator.reliability_policy).map(([key,value])=>[`Policy: ${label(key)}`,key==='excluded_quality_flags'?flags(value):String(value)])]);
      fields('integrity',[['CSV pairing','Verified: sample count, contiguous indices, byte size and CRC32'],['CRC32',crc],['Nominal duration',`${number(duration,2)} s`],['Host duration',`${number(report.session.host_duration_ms/1000,3)} s`],['Maximum host polling gap',`${number(report.acquisition.max_poll_gap_us/1000,3)} ms`],['Firmware',report.session.firmware_version],['Stop reason',label(report.session.stop_reason)],['Exercise stop marker',report.session.exercise_stop_sample_index===null?'Not recorded':`${number(seconds(report.session.exercise_stop_sample_index),2)} s`],['Annotations',JSON.stringify(report.session.annotations)]]);
      initializeCharts();updateTheme();renderEvents();connectControls();element('range-start').max=duration;element('range-end').max=duration;element('report-footer').textContent=`${report.estimator.version} · ${report.estimator.reliability_policy.version} · Nominal sample-index time`;
      element('report').hidden=false;element('status').hidden=true;setRange(0,duration);setCursor(summary.first_reliable_sample_index===null?0:seconds(summary.first_reliable_sample_index));root.dispatchEvent(new CustomEvent('heart-report-ready'));
    }
    function dispose() {
      if(disposed)return;disposed=true;controller.abort();themeObserver.disconnect();pauseReplay();for(const chart of Object.values(charts))chart.destroy();document.body.classList.remove('heart-session-print');
    }
    initialize().catch(error=>{if(disposed)return;themeObserver.disconnect();element('status').hidden=false;element('status').textContent=`Unable to open report: ${error.message}`;for(const chart of Object.values(charts))chart.destroy();});
    return {dispose};
  }
  return {mount};
})();