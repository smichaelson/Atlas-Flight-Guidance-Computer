/* Atlas instruments: every live value comes from validated USB telemetry.
   Demo is explicit. Browser state never authorizes an energetic output. */
'use strict';
const $ = id => document.getElementById(id);
const token = document.querySelector('meta[name="atlas-token"]').content;
const esc = value => String(value ?? '—').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const number = (value, digits=2) => Number.isFinite(value) ? value.toFixed(digits) : '—';
const finite = value => typeof value === 'number' && Number.isFinite(value);
const age = (s, stamp) => (s.ms-stamp)>>>0;
let state = null, networkFailed = false, toastTimer, currentView = 'overview', confirming = false;
const views = {
  overview:['INSTRUMENTS / 01','Mission overview','A clear view of every system, from the bench.','Overview'],
  sensors:['INSTRUMENTS / 02','Sensor observatory','Measurements, timestamps and diagnostics, side by side.','Sensors'],
  gnss:['NAVIGATION / 03','Position & timing','GNSS fixes, local ground track and pulse-per-second timing.','Navigation'],
  links:['COMMUNICATIONS / 04','Across the link','Observe USB, BLE and the RFD900x serial modem.','Communications'],
  tests:['BENCH / 05','Test with intention','Run one explicit operation and inspect its result.','Test controls'],
  firmware:['MAINTENANCE / 06','Firmware, simplified','Verified updates through the same USB-C connection.','Firmware'],
  capture:['EVIDENCE / 07','The session record','Keep the measurements and the context together.','Session log']
};
function showView(view){
  if(!views[view]) view='overview';
  currentView=view;
  document.querySelectorAll('.view').forEach(el=>el.hidden=el.id!==view);
  document.querySelectorAll('nav [data-view]').forEach(el=>{el.classList.toggle('active',el.dataset.view===view);el.setAttribute('aria-current',el.dataset.view===view?'page':'false');});
  const [eyebrow,title,description,crumb]=views[view];
  $('eyebrow').textContent=eyebrow;
  $('page-title').innerHTML=esc(title)+'<span class="title-dot">.</span>';
  $('page-description').textContent=description;$('crumb').textContent=crumb;
  history.replaceState(null,'','#'+view);
  if(state) render(state);
}
function toast(message){$('toast').textContent=message;$('toast').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').hidden=true,7000);}
async function api(action,body){
  const options={headers:{'X-Atlas-Token':token}};
  if(body!==undefined){options.method='POST';options.headers['Content-Type']='application/json';options.body=JSON.stringify(body);}
  const response=await fetch('/api/'+action,options);
  const result=await response.json();
  if(!response.ok)throw new Error(result.error||'Request failed');
  return result;
}
async function act(action,body={}){try{await api(action,body);await pollOnce();return true;}catch(error){toast(error.message);return false;}}
function confirmAction(title,copy){
  if(confirming)return Promise.resolve(false);
  confirming=true;$('confirm-title').textContent=title;$('confirm-copy').textContent=copy;
  const dialog=$('confirm-dialog');dialog.returnValue='cancel';dialog.showModal();
  return new Promise(resolve=>dialog.addEventListener('close',()=>{confirming=false;resolve(dialog.returnValue==='confirm');},{once:true}));
}
function usable(s){return !!s&&!!state?.fresh&&!networkFailed&&!state?.blocked;}
function sensorGood(s,index,key){return !!(s.attempted&(1<<index))&&!s.init[index]&&[0,4].includes(s.sample_status[index])&&s.count[index]>0&&age(s,s[key].t)<=1500;}
function railValue(s,index){return s&&s.power.available&&!s.power.status&&s.power.count>0&&age(s,s.power.t)<=500&&(s.power.valid&(1<<index))?s.power.mv[index]/1000:null;}
function gnssGood(s){const g=s.gnss;return !!(s.attempted&32)&&!s.init[6]&&!s.init[7]&&g.frames>0&&age(s,g.t)<=2500&&!!(g.flags&1)&&[3,4].includes(g.fix)&&finite(g.lat_e7)&&Math.abs(g.lat_e7)<=900000000&&finite(g.lon_e7)&&Math.abs(g.lon_e7)<=1800000000;}
function orientation(s){
  if(!s||!(s.attempted&16)||s.init[4]||s.init[5]||!s.bno.count[3]||age(s,s.bno.t[3])>1500||!s.bno.q_ppm.every(finite))return null;
  let q=s.bno.q_ppm.map(v=>v/1e6);const norm=Math.hypot(...q);if(norm<.5||norm>1.5)return null;q=q.map(v=>v/norm);
  const [w,x,y,z]=q,d=180/Math.PI;
  return {roll:Math.atan2(2*(w*x+y*z),1-2*(x*x+y*y))*d,pitch:Math.asin(Math.max(-1,Math.min(1,2*(w*y-z*x))))*d,yaw:(Math.atan2(2*(w*z+x*y),1-2*(y*y+z*z))*d+360)%360};
}
function metric(id,value,unit,digits=2){$(id).innerHTML=number(value,digits)+' <small>'+esc(unit)+'</small>';}
function details(id,pairs){$(id).innerHTML=pairs.map(([key,value])=>'<div><dt>'+esc(key)+'</dt><dd>'+esc(value)+'</dd></div>').join('');}
function eventsHTML(events){return events.map(e=>'<div class="event '+esc(e.kind)+'"><time>'+esc(e.time)+'</time><span>'+esc(e.message)+'</span></div>').join('')||'<p class="muted">No activity recorded.</p>';}
function attitude(q){
  const roll=q?.roll||0,pitch=q?.pitch||0;
  let ticks='';for(let degrees=-60;degrees<=60;degrees+=10){const a=degrees*Math.PI/180;const r=degrees%30===0?105:110;ticks+=`<line x1="${130+Math.sin(a)*r}" y1="${130-Math.cos(a)*r}" x2="${130+Math.sin(a)*117}" y2="${130-Math.cos(a)*117}" stroke="#809a9e" stroke-width="${degrees%30===0?2:1}"/>`;}
  let ladder='';for(let p=-30;p<=30;p+=10){if(p===0)continue;const y=130-p*2;const wide=Math.abs(p)%20===0?36:22;ladder+=`<path d="M${130-wide} ${y}h${wide*2}" stroke="#c1d5d1" opacity=".65"/><text x="${137+wide}" y="${y+3}" fill="#bbcfcf" font-size="8" font-family="Consolas">${Math.abs(p)}</text>`;}
  $('attitude').innerHTML=`<svg viewBox="0 0 260 246" role="img" aria-label="${q?'Roll '+number(roll,1)+' degrees, pitch '+number(pitch,1)+' degrees':'No valid attitude measurement'}"><defs><clipPath id="horizon-clip"><circle cx="130" cy="130" r="95"/></clipPath></defs><circle cx="130" cy="130" r="119" fill="#0d171c" stroke="#2e414b"/>${ticks}<g clip-path="url(#horizon-clip)" opacity="${q?1:.28}"><g transform="rotate(${-roll} 130 130) translate(0 ${pitch*2})"><rect x="-120" y="-200" width="500" height="330" fill="#1c424b"/><rect x="-120" y="130" width="500" height="360" fill="#35342d"/><path d="M-120 130H380" stroke="#92c2bd" stroke-width="1.5"/>${ladder}</g></g><circle cx="130" cy="130" r="95" fill="none" stroke="#3f565b"/><path d="M66 130h34l8 8h44l8-8h34M130 121v18" stroke="#affbd4" fill="none" stroke-width="2.7"/><circle cx="130" cy="130" r="3" fill="#affbd4"/><path d="M124 8l6 9 6-9" fill="#a8c8ba"/><text x="130" y="239" text-anchor="middle" fill="#8ca2a5" font-family="Consolas" font-size="9">${q?'BOARD ORIENTATION':'NO VALID ATTITUDE'}</text></svg>`;
  for(const key of ['roll','pitch','yaw'])$(key).innerHTML=number(q?.[key],1)+'<em>°</em>';
}
function chart(id,history,series,{unit='',minimumSpan=.1,color='#f2b277'}={}){
  const w=490,h=id==='pressure-chart'?110:174,left=43,right=12,top=10,bottom=24;
  const all=history.flatMap(s=>series.map(fn=>fn(s))).filter(finite);
  let low=all.length?Math.min(...all):0,high=all.length?Math.max(...all):1;
  const center=(low+high)/2,span=Math.max(high-low,minimumSpan)*1.2;low=center-span/2;high=center+span/2;
  const y=v=>top+(high-v)/(high-low)*(h-top-bottom);
  let svg=`<svg viewBox="0 0 ${w} ${h}" preserveAspectRatio="none" role="img" aria-label="${esc(unit)} telemetry history">`;
  for(let i=0;i<4;i++){const v=low+(high-low)*i/3;svg+=`<line x1="${left}" y1="${y(v)}" x2="${w-right}" y2="${y(v)}"/><text x="${left-7}" y="${y(v)+3}" text-anchor="end">${number(v,span<5?2:1)}</text>`;}
  const last=history.at(-1)?.ms||0;const x=s=>w-right-Math.min(120000,age({ms:last},s.ms))/120000*(w-left-right);
  ['−120s','−60s','now'].forEach((t,i)=>svg+=`<text x="${left+(w-left-right)*i/2}" y="${h-3}" text-anchor="${i===0?'start':i===2?'end':'middle'}">${t}</text>`);
  const colors=series.length===1?[color]:['#93edc4','#f2b277','#79bafb'];
  series.forEach((fn,index)=>{let path='',pen=false,previous=null;for(const s of history){const v=fn(s);if(!finite(v)||age({ms:last},s.ms)>120000){pen=false;continue;}if(previous!==null&&age(s,previous)>1500)pen=false;path+=(pen?'L':'M')+number(x(s),1)+' '+number(y(v),1)+' ';pen=true;previous=s.ms;}svg+=`<path d="${path}" fill="none" stroke="${colors[index]}" stroke-width="1.8" vector-effect="non-scaling-stroke"/>`;});
  if(!all.length)svg+=`<text x="${w/2}" y="${h/2}" text-anchor="middle">Waiting for valid samples</text>`;
  $(id).innerHTML=svg+'</svg>';
}
function groundTrack(s,history){
  const samples=history.filter(gnssGood);const current=s&&usable(s)&&gnssGood(s);
  let svg='<svg viewBox="0 0 640 450" role="img" aria-label="Local GNSS ground track">';
  for(let x=40;x<=640;x+=40)svg+=`<path d="M${x} 0v450" stroke="#213039"/>`;
  for(let y=25;y<=450;y+=40)svg+=`<path d="M0 ${y}h640" stroke="#213039"/>`;
  svg+='<path d="M320 0v450M0 225h640" stroke="#3c5058" stroke-dasharray="5 5"/><text x="320" y="21" text-anchor="middle" fill="#93edc4" font-size="13" font-family="Consolas">N</text>';
  if(samples.length){
    const origin=samples[0].gnss,lat=origin.lat_e7/1e7;
    const points=samples.map(({gnss:g})=>{const lonDelta=((g.lon_e7/1e7-origin.lon_e7/1e7+540)%360)-180;return [lonDelta*111320*Math.cos(lat*Math.PI/180),(g.lat_e7/1e7-lat)*111320];});
    const range=Math.max(10,...points.flatMap(p=>p.map(Math.abs)))*1.25;
    const pos=p=>[320+p[0]/range*180,225-p[1]/range*180];
    const path=points.map((p,i)=>(i?'L':'M')+pos(p).map(v=>number(v,2)).join(' ')).join('');
    const [x,y]=pos(points.at(-1));
    svg+=`<path d="${path}" fill="none" stroke="${current?'#93edc4':'#6a8089'}" stroke-width="2"/><circle cx="320" cy="225" r="4" fill="#f2b277"/><circle cx="${x}" cy="${y}" r="13" fill="#93edc4" opacity=".13"/><circle cx="${x}" cy="${y}" r="4" fill="${current?'#93edc4':'#6a8089'}"/><text x="18" y="430" fill="#a3b6c1" font-size="12" font-family="Consolas">${number(range,1)} m / half-height · ${samples.length} fixes${current?'':' · HISTORY ONLY'}</text>`;
  }else svg+='<text x="320" y="211" text-anchor="middle" fill="#b5c7ce" font-size="18">Waiting for a valid 3D fix</text><text x="320" y="243" text-anchor="middle" fill="#8398a3" font-size="13">Position is never inferred from a connected receiver.</text>';
  $('track').innerHTML=svg+'</svg>';
}
function render(v){
  const s=v.status,good=usable(s),live=v.mode==='live',demoMode=v.mode==='demo';
  const badge=$('source-badge');badge.textContent=v.updating?'UPDATING':networkFailed?'SERVER OFFLINE':demoMode?'SIMULATED / DEMO':good&&live?'LIVE / USB':live?'TELEMETRY STALE':'DISCONNECTED';badge.className='badge '+(demoMode?'demo':good&&live?'live':live||networkFailed?'error':'');
  const notice=$('notice');notice.className='notice'+(demoMode?' demo':v.blocked||networkFailed?' error':'');
  notice.textContent=networkFailed?'The local server is unavailable. Measurements and controls are no longer live.':v.updating?v.firmware.message:demoMode?'DEMO MODE · Every measurement is simulated. No hardware is connected and test controls cannot transmit.':v.blocked?v.blocked:good?'USB telemetry active · Battery-powered board · Bench measurements require physical qualification.':live?'USB open. Waiting for a fresh, recognized Atlas Bringup handshake and telemetry.':'Connect Atlas to see measurements, or explore the dashboard with clearly labelled demo data.';
  $('device-version').textContent=v.hello?v.hello.version:'Awaiting firmware identity';
  $('connection-button').textContent=live||demoMode?'Disconnect':'Connect device ↗';$('connection-button').disabled=v.updating;
  $('demo-button').textContent=demoMode?'Exit demo':'Explore demo';$('demo-button').disabled=live||v.updating;
  $('sensor-sequence').disabled=!live||!good||!!v.pending||v.batch.length>0||v.updating;
  document.querySelectorAll('[data-command],#set-utc,#i2c-read').forEach(el=>el.disabled=!live||!good||!!v.pending||v.batch.length>0||v.updating);
  const playing=good&&s.buzzer?.playing===1;
  const march=$('march-button');
  march.disabled=march.disabled||v.hello?.buzzer_melody!==true||!s?.buzzer||playing;
  march.textContent=playing?'♪ Playing Imperial March…':'♪ Imperial March';
  $('update-firmware').disabled=!live||!good||!!v.pending||v.batch.length>0||v.firmware.state!=='checked'||v.hello?.software_dfu!==true||v.updating;
  $('update-firmware').disabled||=playing;
  $('check-firmware').disabled=v.updating;
  $('footer-state').textContent=v.updating?'Firmware update in progress':live?v.port+' · '+(good?'Validated USB telemetry':'Waiting for fresh data'):demoMode?'Simulated instruments · No hardware evidence':'USB-C telemetry · Battery power · No device connected';
  if(currentView==='overview'){
    metric('vin',good?railValue(s,3):null,'V');$('power-state').textContent=good?(railValue(s,3)===null?'ADC unavailable · inspect diagnostics':'ADC reading · verify against a meter'):'Awaiting ADC data';
    const fix=good&&gnssGood(s);metric('altitude',fix&&finite(s.gnss.h_msl_mm)?s.gnss.h_msl_mm/1000:null,'m',1);$('fix-state').textContent=fix?'3D fix · '+s.gnss.sv+' satellites':'No valid 3D fix';
    metric('accel',good&&sensorGood(s,1,'lsm')&&s.lsm.mg.every(finite)?Math.hypot(...s.lsm.mg)/1000:null,'g',3);
    metric('age',v.status&&!networkFailed?v.age_ms:null,'ms',0);$('telemetry-state').textContent=good?'Packet #'+s.seq+' · 2 Hz status stream':'No fresh telemetry';
    const q=good?orientation(s):null;attitude(q);$('attitude-state').textContent=q?'BNO085 · ACCURACY '+s.bno.accuracy[3]:'BNO085 · NO VALID DATA';
    chart('motion-chart',v.history,[0,1,2].map(i=>a=>sensorGood(a,1,'lsm')&&finite(a.lsm.mg[i])?a.lsm.mg[i]/1000:null),{unit:'Acceleration in g',minimumSpan:.2});
    chart('pressure-chart',v.history,[a=>sensorGood(a,3,'baro')&&finite(a.baro.pa)?a.baro.pa/100:null],{unit:'Pressure in hPa',minimumSpan:.5});
    $('motion-chart').style.opacity=$('pressure-chart').style.opacity=good?'1':'.4';
    $('pressure').innerHTML=number(good&&sensorGood(s,3,'baro')&&finite(s.baro.pa)?s.baro.pa/100:null,2)+' <small>hPa</small>';
    $('temperature').textContent='Temperature '+number(good&&sensorGood(s,3,'baro')&&finite(s.baro.temp_cc)?s.baro.temp_cc/100:null,2)+' °C';
    const modules=[['ADXL375','High-g accelerometer',0],['LSM6DSV16B','6-axis inertial sensor',1],['MMC5983MA','Magnetometer',2],['MS5611','Barometric pressure',3],['BNO085','Orientation + fusion',7],['GNSS + PPS','NEO-M9N receiver',8]];
    let count=0;
    $('health-list').innerHTML=modules.map(([name,label,index])=>{let status=good?v.rows[index]?.[1]||'UNKNOWN':'NO DATA';const okay=['RESPONDING','FIX REPORTED'].includes(status);if(okay)count++;const color=okay?'good':status==='FAILED'?'bad':status==='NO DATA'||status==='NOT TESTED'?'':'warn';return `<div class="health-row"><div><strong>${name}</strong><small>${label}</small></div><span class="health-state ${color}">${esc(okay?'RESPONDING':status.replace('RESPONDING / ','').replace('FIX REPORTED','3D FIX'))}</span></div>`;}).join('');
    $('system-count').textContent=count+' / 6 RESPONDING';
    $('latitude').textContent=fix?number(s.gnss.lat_e7/1e7,7)+'°':'—';$('longitude').textContent=fix?number(s.gnss.lon_e7/1e7,7)+'°':'—';
    $('satellites').textContent=good&&s.gnss.frames?s.gnss.sv:'—';$('hacc').textContent=fix?number(s.gnss.hacc_mm/1000,2)+' m':'—';
    $('recent-events').innerHTML=eventsHTML(v.events.slice(-2).reverse());
  }
  if(currentView==='sensors'){
    $('sensor-table').innerHTML=v.rows.map(([name,status,ageValue,detail])=>`<tr><td>${esc(name)}</td><td><span class="health-state ${good&&status==='RESPONDING'?'good':status==='FAILED'?'bad':'warn'}">${esc(good?status:'STALE / LAST OBSERVED')}</span></td><td>${esc(ageValue)} ms</td><td>${esc(detail)}</td></tr>`).join('')||'<tr><td colspan="4">Connect Atlas or explore demo to inspect the sensor schema.</td></tr>';
    $('rail-grid').innerHTML=['3V3_SYS','8V4_PWM','5V_SYS','VIN_PROT','ARMED FEED','CONTINUITY 1','CONTINUITY 2','CONTINUITY 3','CONTINUITY 4','CONTINUITY 5'].map((name,i)=>`<div class="rail"><small>${name}</small><strong>${number(good?railValue(s,i):null,3)} <small style="display:inline">V</small></strong><em>RAW ${good&&railValue(s,i)!==null?s.power.raw[i]:'—'}</em></div>`).join('');
    $('adc-diagnostic').textContent=s?`ADC3 reference stage ${s.power.ref_stage} · ${s.power.ref_channel?'temperature':'VREFINT'} · raw ${s.power.ref_raw} · HAL ${s.power.ref_hal_status} / ${s.power.ref_hal_error} · ADC1 errors ${s.power.adc_errors}`:'No reference diagnostics available.';
  }
  if(currentView==='gnss'){
    groundTrack(s,v.history);const g=good?s.gnss:null,fix=g&&gnssGood(s);
    details('gnss-details',[['Receiver',g?.version||'—'],['Position quality',fix?'3D fix reported':'No valid 3D fix'],['Latitude',fix?number(g.lat_e7/1e7,7)+'°':'—'],['Longitude',fix?number(g.lon_e7/1e7,7)+'°':'—'],['MSL altitude',fix&&finite(g.h_msl_mm)?number(g.h_msl_mm/1000,3)+' m':'—'],['Horizontal accuracy',fix?number(g.hacc_mm/1000,3)+' m':'—'],['Satellites',g?.frames?g.sv:'—'],['NAV frames / CRC errors',g?g.frames+' / '+g.crc_errors:'—'],['PPS count / interval',g?g.pps_count+' / '+g.pps_us+' µs':'—'],['Time of week',g?g.tow_ms+' ms':'—'],['UART RX / TX',g?g.rx_bytes+' / '+g.tx_bytes+' bytes':'—'],['Probe stage / status',g?g.failure_stage+' / '+g.failure_status:'—'],['UART errors / dropped',g?g.uart_errors+' / '+g.dropped:'—']]);
  }
  if(currentView==='links'){
    const r=good?s.radio:null,b=good?s.ble:null,u=good?s.usb:null;
    details('radio-details',[['Transport',good&&s.attempted&128?(s.init[9]?'Initialization failed':'Initialized; peer unverified'):'Not tested'],['UART received',r?r.rx+' bytes':'—'],['Mode',r?(r.command?'Command':'Transparent'):'—'],['Last received bytes',r?.last_hex||'—'],['RSSI / SNR','Not exposed'],['RF delivery','Requires a peer acknowledgement']]);
    details('ble-details',[['Model',b?.model||'—'],['Firmware',b?.firmware||'—'],['Mode',b?(b.command?'Command':'Data'):'—'],['RX / TX',b?b.rx+' / '+b.tx+' bytes':'—'],['DTR / timeouts',b?b.dtr+' / '+b.timeouts:'—'],['Last received bytes',b?.last_hex||'—']]);
    details('usb-details',[['Device',v.port|| (demoMode?'SIMULATED':'—')],['Session',u?.session],['RX / completed TX',u?u.rx+' / '+u.tx+' bytes':'—'],['RX / TX drops',u?u.rx_drop+' / '+u.tx_drop:'—'],['Timeouts',u?.timeouts],['Rejected telemetry',v.decoder_errors]]);
  }
  if(currentView==='tests'){
    $('buzzer-status').textContent=demoMode?'Hardware playback is disabled in demo.':!good?'Connect Atlas with Bringup 1.1.1 or later.':v.hello?.buzzer_melody!==true||!s.buzzer?'Firmware update required · Bringup 1.1.1 or later.':playing?`Playing note ${s.buzzer.note} / ${s.buzzer.notes} · ${s.buzzer.hz?s.buzzer.hz+' Hz':'rest'} · Stop indicators cancels.`:s.buzzer.status?`Last melody stopped with driver status ${s.buzzer.status}.`:'Ready · 33 notes · about 11 seconds · based on your supplied sequence.';
    $('sd-summary').textContent=good?(s.sd.card?'Card detected':'No card')+' · '+(s.sd.mounted?'Mounted':'Unmounted')+' · '+s.sd.completed+' operations · '+s.sd.errors+' errors':'Awaiting storage telemetry.';
    $('operation-status').textContent=v.blocked|| (v.pending?'Running: '+v.pending+(v.batch.length?' · '+v.batch.length+' sensor probes remaining':''):'No operation pending.');
  }
  if(currentView==='firmware'){
    const f=v.firmware;$('firmware-result').textContent=f.message+(f.evidence?'\n\n'+f.evidence.target+' · '+f.evidence.profile+' · '+f.evidence.build_type+'\n'+f.evidence.binary_bytes.toLocaleString()+' bytes · bank 1\nSHA-256 '+f.evidence.hex_sha256:'');
  }
  if(currentView==='capture'){
    $('record-status').textContent=(v.recording?'Recording':'Not recording')+' · '+v.record_count.toLocaleString()+' / 12,000 records'+(v.record_full?' · capture full':'');$('record').textContent=v.recording?'Stop recording':'Start recording';
    $('record').disabled=!v.hello||(!v.recording&&v.record_count>0);$('export').disabled=v.record_count===0;$('clear-record').disabled=v.recording||v.record_count===0;
    $('event-log').innerHTML=eventsHTML(v.events.slice().reverse());$('raw-status').textContent=s?JSON.stringify(s,null,2):'No telemetry.';
  }
}
async function pollOnce(){state=await api('state');networkFailed=false;render(state);}
async function polling(){try{await pollOnce();}catch(error){networkFailed=true;if(state)render(state);else{$('notice').textContent='The local server is unavailable. Start Atlas Ground Station and reload.';}}finally{setTimeout(polling,700);}}
async function ports(){try{const list=await api('ports');$('port-select').innerHTML='<option value="">Select the Atlas port</option>'+list.map(p=>`<option value="${esc(p.device)}">${esc(p.device+' · '+p.description)}</option>`).join('');}catch(error){toast(error.message);}}
async function command(verb){
  let copy='';
  if(verb.startsWith('radio '))copy='This communicates with the RFD900x modem. Confirm the correct radio, antenna, power and regional configuration are ready. Sending test text can transmit RF; it does not prove receipt.';
  else if(verb==='sd test')copy='Create ATLASCHK.TST on the inserted expendable FAT card, write the test pattern and compare it. Existing files are preserved. Do not remove power or the card during the operation.';
  else if(verb.startsWith('ble '))copy='Apply this explicit BLE operation. The SPS profile is volatile. A ping sends fixed test text; verify receipt on your paired client.';
  else if(verb.startsWith('gpio ')&&verb!=='gpio 0')copy='Drive this logic GPIO high for one second. Confirm only the intended inert loopback or measurement fixture is attached.';
  else if(verb==='uart'||verb==='spi'||verb.startsWith('i2c '))copy='Confirm the documented expansion test fixture and voltage levels. This sends bytes on the selected physical bus.';
  else if(verb==='beep')copy='Drive the buzzer for a requested 200 ms pulse. Keep motors, servos and energetic loads disconnected.';
  else if(verb==='march')copy='Play your supplied Imperial March note sequence for about 11 seconds. Stop indicators cancels playback. Keep motors, servos and pyro loads disconnected.';
  else if(verb.startsWith('utc '))copy='Set the board RTC to this laptop’s current UTC. This changes timestamps used by storage operations.';
  if(copy&&!await confirmAction(verb,copy))return;
  await act('command',{verb,action_confirmed:!!copy||verb==='gpio 0'});
}
document.addEventListener('click',event=>{
  const nav=event.target.closest('[data-view]');if(nav){showView(nav.dataset.view);return;}
  const button=event.target.closest('[data-command]');if(button&&!button.disabled)command(button.dataset.command);
});
$('probe-buttons').innerHTML=['adxl','lsm','mmc','baro','bno','gnss'].map(m=>`<button data-command="probe ${m}">${m.toUpperCase()}</button>`).join('');
$('gpio-buttons').innerHTML=Array.from({length:7},(_,i)=>`<button data-command="gpio ${i+1}">${i+1}</button>`).join('')+'<button data-command="gpio 0">All low</button>';
$('connection-button').onclick=async()=>{if(state?.mode!=='disconnected'){await act('disconnect');return;}$('bench-confirm').checked=false;$('connect-dialog').showModal();await ports();};
$('refresh-ports').onclick=ports;
$('connect-confirm').onclick=async()=>{if(await act('connect',{port:$('port-select').value,confirmed:$('bench-confirm').checked}))$('connect-dialog').close();};
$('demo-button').onclick=()=>act(state?.mode==='demo'?'disconnect':'demo');
$('sensor-sequence').onclick=()=>act('sensors');
$('set-utc').onclick=()=>{const d=new Date();command(`utc ${d.getUTCFullYear()} ${d.getUTCMonth()+1} ${d.getUTCDate()} ${d.getUTCHours()} ${d.getUTCMinutes()} ${d.getUTCSeconds()}`);};
$('i2c-read').onclick=()=>{const a=$('i2c-address').value,r=$('i2c-register').value;if(!/^\d+$/.test(a)||!/^\d+$/.test(r)||+a<8||+a>119||+r>255){toast('Enter a decimal address from 8–119 and register from 0–255.');return;}command(`i2c ${+a} ${+r}`);};
$('check-firmware').onclick=()=>act('firmware-check',$('manifest').value.trim()?{manifest:$('manifest').value.trim()}:{});
$('update-firmware').onclick=async()=>{const key=state?.firmware.key;if(await confirmAction('Update Atlas firmware','Flash the verified Bringup image to the identified Atlas. Keep battery power and USB-C connected. SD must be unmounted, J5 open, and motors, servos and pyro loads disconnected.'))await act('firmware-update',{key,action_confirmed:true});};
$('record').onclick=()=>act('record',{enabled:!state?.recording});
$('clear-record').onclick=async()=>{if(await confirmAction('Clear this capture','Remove the in-memory capture from this local session. Export it first if you want to keep it.'))await act('clear-record');};
$('export').onclick=async()=>{try{
  const r=await fetch('/api/capture',{headers:{'X-Atlas-Token':token}});
  if(!r.ok)throw new Error('Capture export failed.');
  const content=await r.text();
  // Capture provenance survives disconnect and later changes of display mode.
  const simulated=content.split('\n').some(line=>line.trim()&&JSON.parse(line).source==='demo');
  const blob=new Blob([content],{type:'application/x-ndjson'}),url=URL.createObjectURL(blob),a=document.createElement('a');
  a.href=url;a.download='atlas-'+(simulated?'SIMULATED-':'')+new Date().toISOString().replace(/[:.]/g,'-')+'.jsonl';
  a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}catch(error){toast(error.message);}};
setInterval(()=>$('clock').textContent=new Date().toISOString().slice(11,19)+' UTC',1000);
window.addEventListener('hashchange',()=>showView(location.hash.slice(1)));
showView(location.hash.slice(1));polling();

// A small read-only agent surface shares exactly the visible session model.
// No browser tool can issue a hardware command or authorize firmware writes.
if(document.modelContext?.registerTool){
  const lifecycle=new AbortController();
  try{Promise.resolve(document.modelContext.registerTool({
    name:'atlas_read_telemetry',title:'Read Atlas telemetry',
    description:'Read the current Atlas source, freshness and subsystem observations. Demo observations are simulated, not hardware evidence.',
    inputSchema:{type:'object',properties:{},additionalProperties:false},
    annotations:{readOnlyHint:true,untrustedContentHint:true},
    execute:async input=>{
      if(!input||Array.isArray(input)||typeof input!=='object'||Object.keys(input).length)throw new Error('This tool accepts an empty object.');
      await pollOnce();
      return {source:state.mode,fresh:state.fresh,age_ms:state.age_ms,blocked:state.blocked,observations:state.rows};
    }
  },{signal:lifecycle.signal})).catch(()=>{});}catch(_){/* UI works without WebMCP. */}
  window.addEventListener('pagehide',()=>lifecycle.abort(),{once:true});
}
