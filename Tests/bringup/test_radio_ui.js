// Run the production radio presentation with inert DOM objects, including stale evidence.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const elements=new Map();const element=id=>{if(!elements.has(id))elements.set(id,{disabled:false});return elements.get(id);};
const context=vm.createContext({window:{},document:{getElementById:element},details:()=>{}});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../../tools/bringup/web/radio.js'),'utf8'),context);
const radio=context.window.AtlasRadio;
const base={mode:'live',fresh:true,pending:null,batch:[],age_ms:0,hello:{radio_link:1},
 status:{ms:1000,owner_ms:1000,attempted:128,init:Array(12).fill(0),gpio:{pwm:0},radio:{
 connected:0,monitoring:0,waiting:0,test:0,test_sequence:1,test_peer:[1,2,3],peer:[1,2,3],rtt_ms:85,
 test_rtt_ms:85,ack_age_ms:0xFFFFFFFF,baud:57600,timeouts:0,rx:0,command:0}}};
function check(edit=()=>{},good=true){const v=structuredClone(base);edit(v);radio.render(v,good);return radio.presentation(v,good);}
assert.equal(check().label,'Not connected');
assert.equal(check(v=>Object.assign(v.status.radio,{monitoring:1,timeouts:1})).label,'No peer response');
assert.equal(check(v=>Object.assign(v.status.radio,{monitoring:0,timeouts:31,test:2})).label,'Not connected');
let m=check(v=>Object.assign(v.status.radio,{connected:1,ack_age_ms:100,test:2}));
assert.equal(m.connected,true);assert.match(m.result,/reply from 00000001-00000002-00000003 in 85 ms/);
for(const edit of [v=>v.mode='demo',v=>v.mode='disconnected',v=>v.status.owner_ms=0,
 v=>v.status.init[9]=4,v=>v.status.radio.command=1,v=>delete v.hello.radio_link,
 v=>v.status.radio.ack_age_ms=6000,v=>{v.status.radio.ack_age_ms=5990;v.age_ms=20;}]){
 const v=structuredClone(base);Object.assign(v.status.radio,{connected:1,ack_age_ms:100});edit(v);
 assert.equal(radio.presentation(v,true).connected,false);
}
assert.equal(check(v=>Object.assign(v.status.radio,{connected:1,ack_age_ms:100}),false).connected,false);
assert.match(check(v=>Object.assign(v.status.radio,{test:3,timeouts:1})).result,/no reply within 2 seconds/);
assert.match(check(v=>v.status.radio.test=4).result,/transmission failed/);
assert.match(check(v=>v.status.radio.test=2,false).result,/Last recorded result/);
assert.equal(check(v=>{v.status.gpio.pwm=0xC3;v.status.stabilization={enabled:1};}).enabled,true);
for(const edit of [v=>v.pending='radio ping',v=>v.batch=['probe lsm'],v=>v.updating=true,v=>v.blocked='fault'])
 assert.equal(check(edit).enabled,false);
check(v=>v.status.radio.monitoring=1);assert.equal(element('radio-connect').disabled,true);
assert.equal(element('radio-test').disabled,false);assert.equal(element('radio-stop').disabled,false);
const html=fs.readFileSync(path.join(__dirname,'../../tools/bringup/web/index.html'),'utf8');
for(const id of ['radio-status','radio-test-result','radio-connect','radio-test','radio-stop','radio-probe','radio-identity'])
 assert.match(html,new RegExp('id="'+id+'"'));
assert.match(html,/src="\/radio.js"/);
console.log('PASS: radio connection/test rendering, legacy/demo/stale/owner/age gates, stable test result, monitoring controls and HTML wiring');
// Stopping monitoring is an explicit button action and must satisfy the HTTP command gate.
const commandContext=vm.createContext({window:{},document:{querySelector:()=>({content:'test-token'})}});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../../tools/bringup/web/app.js'),'utf8').split("document.addEventListener('click'")[0],commandContext);
vm.runInContext('var recorded; act=async(action,body)=>{recorded={action,body};}; command("radio disconnect");',commandContext);
assert.equal(vm.runInContext('recorded.action',commandContext),'command');
assert.equal(vm.runInContext('recorded.body.action_confirmed',commandContext),true);
console.log('PASS: Stop monitoring sends the deliberate HTTP acknowledgement without another modal');
