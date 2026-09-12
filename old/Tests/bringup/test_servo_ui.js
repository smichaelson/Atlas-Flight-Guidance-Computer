// Exercise production angle conversions and UI events against an inert DOM.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.join(__dirname,'../../tools/bringup/web');
function element(value=''){
  return {value,checked:false,disabled:false,textContent:'',innerHTML:'',dataset:{},attrs:{},handlers:{},
    classList:{toggle(){}},setAttribute(k,v){this.attrs[k]=v;},addEventListener(k,fn){this.handlers[k]=fn;}};
}
function fixture(){
  let now=0;const sent=[],messages=[],els=new Map(),windowEvents={},documentEvents={},fetches=[];
  const $=id=>{if(!els.has(id))els.set(id,element());return els.get(id);};
  const cards=Array.from({length:8},(_,i)=>{const c=element(),children=new Map();c.dataset.channel=i+1;
    c.querySelector=k=>{if(!children.has(k))children.set(k,element());return children.get(k);};return c;});
  const buttons=[-50,-25,0,25,50].map(a=>{const b=element();b.dataset.servoAngle=String(a);return b;});
  const state={mode:'live',generation:1,servo_control_epoch:2,fresh:true,age_ms:0,pending:null,batch:[],updating:false,
    hello:{profile:'servo_bench',servo_test:true,servo_layout:1,servo_direct:true,servo_adc_samples:16,servo_pwm_max_mv:8550},
    status:{seq:1,ms:1000,pending_id:0,gpio:{pwm:1},servo:{ready:1,remaining_ms:3000,stop_reason:0,
      min_us:1000,max_us:2000,pulse_us:[1520,0,0,0,0,0,0,0],target_us:1520},power:{t:1000,mv:[3360,8420]}}};
  const context=vm.createContext({window:{addEventListener:(k,f)=>windowEvents[k]=f},
    document:{hidden:false,activeElement:null,addEventListener:(k,f)=>documentEvents[k]=f,
      querySelectorAll:s=>s==='.servo-card'?cards:s==='[data-servo-angle]'?buttons:[]},
    state,currentView:'servos',performance:{now:()=>now},$,finite:Number.isFinite,
    number:(n,d)=>Number(n).toFixed(d),age:(s,t)=>s.ms-t,railValue:(s,i)=>s.power.mv[i]/1000,
    usable:s=>!!s&&state.fresh&&!state.blocked,toast:s=>messages.push(s),token:'inert',
    fetch:(...args)=>{fetches.push(args);return Promise.resolve();},
    setTimeout:()=>{throw new Error('No timer may queue or repeat a discrete move');},
    act:async(action,body)=>{sent.push({action,body});return true;}});
  vm.runInContext(fs.readFileSync(path.join(root,'servo_motion.js'),'utf8'),context);
  vm.runInContext(fs.readFileSync(path.join(root,'servos.js'),'utf8'),context);
  context.window.AtlasServos.render(state);
  const settle=async()=>{await Promise.resolve();await Promise.resolve();};
  const type=value=>{$('servo-angle').value=String(value);$('servo-angle').handlers.input();};
  const submit=async value=>{type(value);$('servo-angle-form').handlers.submit({preventDefault(){}});await settle();};
  const click=async angle=>{const b=buttons.find(b=>Number(b.dataset.servoAngle)===angle);
    $('servo-angle-options').handlers.click({target:{closest:()=>b}});await settle();};
  return {context,state,$,cards,buttons,sent,messages,fetches,windowEvents,documentEvents,type,submit,click,settle,advance:ms=>now+=ms};
}
(async()=>{
  let f=fixture();const a=f.context.window.AtlasServoAngles;
  assert.equal(a.toPulse(-50),1000);assert.equal(a.toPulse(0),1500);assert.equal(a.toPulse(50),2000);assert.equal(a.fromPulse(1520),2);
  for(let angle=-50;angle<=50;angle++)assert.equal(a.fromPulse(a.toPulse(angle)),angle);
  for(const bad of [NaN,Infinity,null,undefined,true,'50',-51,51,0.5])assert.equal(a.toPulse(bad),null);
  assert.match(f.cards[0].querySelector('.servo-command-value').textContent,/MCU \+2° · 1520 µs/);
  f.type(35);f.type(-22);assert.equal(f.sent.length,0);assert.equal(f.$('servo-preview-pulse').textContent,'1280 µs');
  await f.submit(-22);assert.equal(f.sent.length,1);assert.deepEqual(JSON.parse(JSON.stringify(f.sent[0].body)),
    {operation:'set',channel:1,generation:1,control_epoch:2,pulse_us:1280});
  await f.click(-50);await f.click(0);await f.click(50);
  assert.deepEqual(f.sent.slice(1).map(x=>x.body.pulse_us),[1000,1500,2000]);
  f.advance(5000);await f.settle();assert.equal(f.sent.length,4); // No keepalive or repeated movement.
  for(const bad of ['',51,-51,0.5,'NaN']){f=fixture();await f.submit(bad);assert.equal(f.sent.length,0);}
  for(const change of [f=>f.state.mode='demo',f=>f.state.fresh=false,f=>f.state.blocked='fault',
    f=>f.state.status.gpio.pwm=0,f=>f.state.hello.servo_layout=0,f=>f.context.document.hidden=true,
    f=>delete f.state.hello.servo_direct,f=>f.state.hello.servo_direct=false,f=>f.state.hello.servo_direct=1,
    f=>f.context.currentView='overview',f=>f.state.pending='servo set',f=>f.state.status.pending_id=12,
    f=>f.state.batch=['probe'],f=>f.state.updating=true,f=>f.state.age_ms=1001,
    f=>f.state.status.servo.ready=0,f=>f.state.status.power.mv[1]=8551,f=>f.state.status.power.mv[1]=0,
    f=>f.advance(1001)]){
    f=fixture();change(f);await f.submit(25);assert.equal(f.sent.length,0);
  }
  f=fixture();f.state.status.servo.min_us=1440;f.state.status.servo.max_us=1600;
  await f.submit(50);assert.equal(f.sent.length,0);await f.submit(5);assert.equal(f.sent[0].body.pulse_us,1550);
  f=fixture();delete f.state.hello.servo_direct;f.context.window.AtlasServos.render(f.state);
  assert.equal(f.$('servo-enable').disabled,true);assert.equal(f.$('servo-stop').disabled,false);
  await f.$('servo-stop').onclick();assert.equal(f.sent[0].action,'servo-stop');
  f=fixture();let release;
  f.context.act=(action,body)=>{f.sent.push({action,body});return action==='servo'?new Promise(r=>release=r):Promise.resolve(true);};
  await f.click(25);await f.click(-50);assert.equal(f.sent.length,1);
  await f.$('servo-stop').onclick();release(true);await f.settle();await f.submit(-25);
  assert.deepEqual(f.sent.map(x=>x.action),['servo','servo-stop']); // Stop wins; no retained click replays.
  f=fixture();f.context.window.AtlasServos.leaving('servos','sensors');assert.equal(f.fetches.length,1);
  await f.submit(25);assert.equal(f.sent.length,0);
  f=fixture();f.state.status.gpio.pwm=0;f.context.window.AtlasServos.render(f.state);f.$('servo-confirm').checked=true;
  f.context.window.AtlasServos.render(f.state);await f.submit(-50);assert.equal(f.sent.length,0);
  await f.$('servo-enable').onclick();assert.equal(f.sent.length,1);assert.equal(f.sent[0].body.operation,'enable');
  assert.equal(f.sent[0].body.minimum_us,1000);assert.equal(f.sent[0].body.maximum_us,2000);
  assert.equal(f.$('servo-preview-value').textContent,'+2°'); // Existing firmware enable is 1520, not nominal zero.
  assert.doesNotMatch(fs.readFileSync(path.join(root,'index.html'),'utf8'),/type="range"|role="slider"/);
  assert.doesNotMatch(f.$('servo-models').innerHTML,/role="slider"|data-servo-dial/);
  console.log('PASS servo angles: complete ±50° mapping, one request per selection, typing-only preview, no queue/replay, Stop/freshness/voltage/session gates and exact 1000–2000 µs enable bounds.');
})().catch(e=>{console.error(e);process.exitCode=1;});
