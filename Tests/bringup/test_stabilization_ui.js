/* Inert widget integration plus the real compact polling regression. */
'use strict';
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict'),path=require('path');
const web=path.join(__dirname,'../../tools/bringup/web');
function fixture(){
  const els=new Map(),sent=[],$=id=>{if(!els.has(id))els.set(id,{checked:false,disabled:false,attributes:{},setAttribute(k,v){this.attributes[k]=v;}});return els.get(id);};
  const state={mode:'live',hello:{stabilization:true,stabilization_layout:1},fresh:true,age_ms:5,generation:1,servo_control_epoch:3,
    pending:null,batch:[],updating:false,status:{pending_id:0,seq:1,gpio:{switch:0,pwm:0},sd:{card:1},servo:{pulse_us:Array(8).fill(0)},
      stabilization:{enabled:0,calibrated:1,calibration_ready:1,saved:1,save_busy:0,state:1,reverse_mask:0}}};
  const c=vm.createContext({window:{},state,$,usable:s=>!!s&&state.fresh,number:(v,d)=>v.toFixed(d),act:async(action,body)=>{sent.push({action,body});return true;}});
  vm.runInContext(fs.readFileSync(path.join(web,'stabilization.js'),'utf8'),c);
  c.window.AtlasStabilization.render(state);$('stab-confirm').checked=true;c.window.AtlasStabilization.render(state);
  return {c,$,state,sent,render:()=>c.window.AtlasStabilization.render(state)};
}
(async()=>{
  let f=fixture();assert(!f.$('stab-toggle').disabled);assert.equal(f.sent.length,0);
  f.$('stab-toggle').checked=true;await f.$('stab-toggle').onchange();await Promise.resolve();
  assert.equal(f.sent[0].body.operation,'on');assert.equal(f.sent[0].body.control_epoch,3);
  for(const change of [f=>f.state.mode='demo',f=>f.state.fresh=false,f=>f.state.status.gpio.switch=1,
      f=>f.state.status.gpio.pwm=0xC3,f=>f.state.status.sd.card=0,f=>f.state.pending='stabilize on',
      f=>f.state.status.stabilization.save_busy=1,f=>f.state.age_ms=1001,f=>f.$('stab-confirm').checked=false]){
    f=fixture();change(f);f.render();assert(f.$('stab-toggle').disabled);
    f.$('stab-toggle').checked=true;await f.$('stab-toggle').onchange();assert.equal(f.sent.length,0);
  }
  f=fixture();f.state.status.stabilization.enabled=1;f.state.status.gpio.switch=1;f.state.fresh=false;f.render();
  assert(!f.$('stab-toggle').disabled);f.$('stab-toggle').checked=false;await f.$('stab-toggle').onchange();
  assert.equal(f.sent[0].body.operation,'off');
  f=fixture();f.$('stab-reverse-7').checked=true;f.$('stab-reverse-7').onchange();await f.$('stab-save-directions').onclick();
  assert.equal(f.sent[0].body.reverse_mask,1);
  // Illustrations follow the commissioned shaft direction; numeric angles retain
  // the pulse convention used by the manual workbench (1500 us = 0 degrees).
  f=fixture();f.state.status.gpio.pwm=0xC3;
  f.state.status.servo.pulse_us=[1000,2000,0,0,0,0,2000,1000];
  f.state.status.stabilization.reverse_mask=5;f.render();
  assert.equal(f.$('stab-horn-7').attributes.transform,'rotate(-50 50 38)');
  assert.equal(f.$('stab-horn-8').attributes.transform,'rotate(-50 50 38)');
  assert.equal(f.$('stab-horn-1').attributes.transform,'rotate(50 50 38)');
  assert.equal(f.$('stab-horn-2').attributes.transform,'rotate(50 50 38)');
  assert.equal(f.$('stab-angle-7').textContent,'50.0°');
  f=fixture();f.state.status.stabilization.enabled=1;
  Object.assign(f.state.status.stabilization,{state:5,reason:7,fault_detail:3});f.render();
  assert(f.$('stab-detail').textContent.includes('Rotation exceeded 500°/s'));
  delete f.state.status.stabilization.fault_detail;f.render();
  assert(f.$('stab-detail').textContent.includes('IMU data invalid or stale'));
  // The app formerly discarded compact replies whenever the view was not "servos".
  const source=fs.readFileSync(path.join(web,'app.js'),'utf8');
  const poll=source.slice(source.indexOf('let stateReadSerial='),source.indexOf('async function polling'));
  const p=vm.createContext({currentView:'stabilization',api:async()=>({mode:'live'}),render:()=>{},networkFailed:true,state:null});
  vm.runInContext(poll,p);await vm.runInContext('pollOnce()',p);assert.equal(p.state.mode,'live');
  console.log('PASS stabilization UI: deliberate toggle, demo/stale/SW2/card/pending gates, stale OFF, direction mask and real compact polling');
})().catch(e=>{console.error(e);process.exit(1);});
