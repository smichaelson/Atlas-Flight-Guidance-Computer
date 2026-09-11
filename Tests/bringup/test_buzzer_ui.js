// Render production control gates against inert element objects; no browser or hardware.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const elements=new Map();
function element(id){if(!elements.has(id))elements.set(id,{disabled:false,textContent:''});return elements.get(id);}
const context=vm.createContext({window:{},document:{querySelector:()=>({content:'inert-token'}),
  getElementById:element,querySelectorAll:()=>['march-button','beep','stop'].map(element)}});
const source=fs.readFileSync(path.join(__dirname,'../../tools/bringup/web/app.js'),'utf8').split("document.addEventListener('click'")[0];
vm.runInContext(source,context);
const fixture={mode:'live',fresh:true,blocked:'',pending:null,batch:[],updating:false,
  hello:{version:'1.1.1',buzzer_melody:true,software_dfu:true},firmware:{state:'checked'},
  status:{sd:{},power:{available:false},gpio:{pwm:0},buzzer:{playing:0,note:0,notes:33,hz:0,status:0}}};
function render(edit=''){
  vm.runInContext(`currentView='tests';state=${JSON.stringify(fixture)};${edit};render(state);`,context);
}
render();assert.equal(element('march-button').disabled,false);
assert.match(element('buzzer-status').textContent,/Ready/);
for(const edit of ["state.mode='demo'","state.fresh=false","state.blocked='uncertain'",
  "state.pending='probe gnss'","state.batch=['probe bno']","state.updating=true",
  "delete state.hello.buzzer_melody","delete state.status.buzzer"]){
  render(edit);assert.equal(element('march-button').disabled,true,edit);
}
render('state.status.buzzer.playing=1;state.status.buzzer.note=3;state.status.buzzer.hz=1568');
assert.equal(element('march-button').disabled,true);
assert.equal(element('stop').disabled,false);
assert.equal(element('update-firmware').disabled,true);
assert.match(element('buzzer-status').textContent,/Playing note 3 \/ 33/);
render('state.status=null;state.fresh=false');assert.equal(element('march-button').disabled,true);
console.log('PASS: buzzer UI readiness, demo/stale/capability/pending gates, playback display and interruptible Stop.');
