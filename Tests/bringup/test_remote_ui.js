// Exercise actual remote rendering with a validated Python/C-wire fixture and an inert DOM.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),assert=require('node:assert/strict');
const root=path.join(__dirname,'../..');
if(!process.argv[2])throw new Error('Pass the JSON fixture produced by test_remote_station.py --fixture');
const base=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const nodes=new Map(),detailsMap=new Map(),metrics=new Map();
const $=id=>{if(!nodes.has(id))nodes.set(id,{disabled:false});return nodes.get(id);};
const number=(v,d=2)=>Number.isFinite(v)?v.toFixed(d):'—';
const context=vm.createContext({window:{},$,number,esc:x=>String(x),metric:(id,value)=>metrics.set(id,value),details:(id,values)=>detailsMap.set(id,values)});
vm.runInContext(fs.readFileSync(path.join(root,'tools/bringup/web/remote.js'),'utf8'),context);
const remote=context.window.AtlasRemote;
function run(edit=()=>{},usable=true){const v=structuredClone(base);edit(v);remote.render(v,usable);return remote.presentation(v,usable);}
let m=run();assert.equal(m.fresh,true);assert.equal(m.fix,true);assert.equal(m.adc,true);
assert.equal(metrics.get('remote-age'),120);assert.equal(metrics.get('remote-loss'),0);
assert.match($('remote-identity').textContent,/00000007-00000008-00000009/);
assert.match($('remote-rails').innerHTML,/8\.200/);assert.match($('remote-sensors').innerHTML,/-0\.200/);
assert.equal(detailsMap.get('remote-gnss').find(([k])=>k==='Longitude')[1],'-117.0000000°');
for(const change of [v=>v.mode='demo',v=>v.mode='disconnected',v=>delete v.hello.remote_telemetry,
 v=>v.remote_age_ms=2501,v=>v.remote.owner_ms=1000,v=>v.status.owner_ms=1000,
 v=>v.remote.received_ms=(v.remote.ms-3500)>>>0,v=>v.remote.available=0,v=>v.remote=null]){
 m=run(change);assert.equal(!!m.fresh,false);assert.doesNotMatch($('remote-rails').innerHTML,/8\.200/);
 assert.equal(detailsMap.get('remote-gnss').find(([k])=>k==='Latitude')[1],'—');
}
assert.equal(!!run(()=>{},false).fresh,false);
m=run(v=>{v.remote.data.gnss.fix=0;v.remote.data.power.valid=0;});assert.equal(m.fix,false);
assert.doesNotMatch($('remote-rails').innerHTML,/8\.200/);
m=run(v=>{v.status.power.mv.fill(12345);});assert.match($('remote-rails').innerHTML,/8\.200/);
assert.doesNotMatch($('remote-rails').innerHTML,/12\.345/);
m=run(v=>{v.remote.stats.missing_packets=2;v.remote.stats.expected_packets=10;v.remote.stats.crc_errors=1;});
assert.equal(m.loss,20);assert.equal(m.crc,100/11);
run();assert.equal($('remote-pause').disabled,false);assert.equal($('remote-resume').disabled,true);
run(v=>v.remote.streaming=0);assert.equal($('remote-pause').disabled,true);assert.equal($('remote-resume').disabled,false);
for(const change of [v=>v.pending='status',v=>v.updating=true,v=>v.blocked='fault',v=>v.batch=['probe lsm']]){
 run(change);assert.equal($('remote-pause').disabled,true);assert.equal($('remote-resume').disabled,true);
}
const html=fs.readFileSync(path.join(root,'tools/bringup/web/index.html'),'utf8');
assert.match(html,/data-view="remote"/);assert.match(html,/src="\/remote.js"/);
for(const id of nodes.keys())assert.ok(html.includes(`id="${id}"`),id);
const cc=vm.createContext({window:{},document:{querySelector:()=>({content:'test-token'})}});
vm.runInContext(fs.readFileSync(path.join(root,'tools/bringup/web/app.js'),'utf8').split("document.addEventListener('click'")[0],cc);
for(const command of ['telemetry on','telemetry off']){
 vm.runInContext(`var recorded; act=async(action,body)=>{recorded={action,body};}; command(${JSON.stringify(command)});`,cc);
 assert.equal(vm.runInContext('recorded.body.action_confirmed',cc),true);
}
console.log('PASS remote UI: source isolation, freshness/legacy/demo gates, GPS validity, ADC validity, measured rates/loss, local broadcast controls and wiring');
