/* Discrete angle commands: one explicit selection, one request, no queued motion. */
'use strict';
window.AtlasServos = (() => {
  const angles=window.AtlasServoAngles;
  let selected=1,generation=null,busy=false,halted=false,observedSeq=null,observedAt=-Infinity;
  const preview=Array(8).fill(0);
  const pins=['J19 · PB0','J20 · PB1','J21 · PE9','J6 · PE11','J15 · PE13','J16 · PE14','J17 · PC6','J18 · PC7'];
  const stopReasons=['Output idle','Stopped by operator','Time limit reached','USB session ended','Rail interlock','Monitor fault'];
  const angleText=value=>(value>0?'+':'')+number(value,Number.isInteger(value)?0:1)+'°';
  const model = ch => `<div class="servo-card" data-channel="${ch}"><button class="servo-select" data-servo-select="${ch}"><span>PCB PWM ${ch}</span><span class="servo-pin">${pins[ch-1]}</span></button>
    <svg class="servo-model" viewBox="0 0 240 250" role="img" aria-label="PCB PWM ${ch} servo illustration">
    <defs><linearGradient id="case-${ch}" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#448dc8"/><stop offset="1" stop-color="#174466"/></linearGradient></defs>
    <path d="M58 106a62 62 0 0 1 124 0" class="servo-arc"/><path d="M120 33v10M65 74l9 5M175 74l-9 5" class="servo-ticks"/>
    <ellipse cx="125" cy="227" rx="58" ry="8" fill="#050c11" opacity=".5"/>
    <path d="M103 214v18m8-18v18m8-18v18" stroke="#59453a" stroke-width="3"/>
    <rect x="81" y="103" width="78" height="116" rx="9" fill="#0a1b28"/>
    <rect x="69" y="107" width="102" height="13" rx="3" fill="#27668d"/><circle cx="76" cy="114" r="3" fill="#0c202d"/><circle cx="164" cy="114" r="3" fill="#0c202d"/>
    <rect x="84" y="107" width="70" height="107" rx="7" fill="url(#case-${ch})" stroke="#6395b8" stroke-width="1"/>
    <path d="M87 133h64M87 143h64M87 191h64M87 201h64" stroke="#112d40" opacity=".6"/>
    <text x="119" y="163" text-anchor="middle" fill="#e0eef6" font-family="system-ui" font-size="15" font-weight="700">KST</text><text x="119" y="180" text-anchor="middle" fill="#b9d2e2" font-family="monospace" font-size="10">X10 V8.0</text>
    <circle cx="120" cy="105" r="16" fill="#a9894e" stroke="#e1c68d"/><circle cx="120" cy="105" r="10" fill="#573f21"/>
    <g class="servo-command-horn" transform="rotate(0 120 105)" visibility="hidden"><path d="M120 105V51" stroke="#91edc5" stroke-width="5" stroke-linecap="round"/></g>
    <g class="servo-preview-horn" transform="rotate(0 120 105)"><path d="M113 106l2-58a5 5 0 0 1 10 0l2 58z" fill="#ddad77" stroke="#f3c99d"/><circle cx="120" cy="52" r="2.5" fill="#483320"/><circle cx="120" cy="65" r="2.5" fill="#483320"/><circle cx="120" cy="105" r="5" fill="#d3dbe0"/></g>
    </svg><div class="servo-card-reading"><strong class="servo-draft">1520 <small>µs</small></strong><span class="servo-command-value">PREVIEW ONLY</span></div></div>`;
  $('servo-models').innerHTML=Array.from({length:8},(_,i)=>model(i+1)).join('');
  function active(){return state?.status?.gpio?.pwm||0;}
  function capable(){return state?.mode==='live'&&state.hello?.profile==='servo_bench'&&state.hello?.servo_test===true;}
  function policy(){return capable()&&state.hello.servo_pwm_max_mv===8550&&state.hello.servo_layout===1&&state.hello.servo_direct===true&&state.hello.servo_adc_samples===16;}
  function canMove(pulse=angles.toPulse(preview[selected-1])){
    const s=state?.status;
    const pwm=usable(s)?railValue(s,1):null;
    return !halted&&!busy&&policy()&&usable(s)&&s.servo.ready&&finite(pwm)&&pwm>0&&pwm<=8.55&&age(s,s.power.t)<=100&&active()===(1<<(selected-1))&&
      performance.now()-observedAt<=1000&&state.age_ms<=1000&&!document.hidden&&currentView==='servos'&&
      !state.pending&&!s.pending_id&&!state.batch.length&&!state.updating&&pulse!==null&&
      pulse>=s.servo.min_us&&pulse<=s.servo.max_us;
  }
  function select(ch){
    if(active()&&active()!==(1<<(ch-1))){toast('Stop the current channel before selecting another.');return;}
    selected=ch;paint();
  }
  function draft(raw,syncInput=true){
    if(typeof raw==='string'&&!raw.trim())return false;
    const angle=Number(raw);
    if(angles.toPulse(angle)===null)return false;
    preview[selected-1]=angle;paint(syncInput);return true;
  }
  async function choose(raw){
    if(busy)return;
    if(!draft(raw)){toast('Choose a whole-degree angle from −50° to +50°.');return;}
    const pulse=angles.toPulse(preview[selected-1]);
    if(!canMove(pulse))return;
    const context={channel:selected,generation:state.generation,control_epoch:state.servo_control_epoch};
    busy=true;paint();
    try{await act('servo',{operation:'set',...context,pulse_us:pulse});}
    finally{busy=false;render(state);}
  }
  function paint(syncInput=true){
    const s=state?.status,valid=state&&usable(s),mask=valid?active():0;
    document.querySelectorAll('.servo-card').forEach(card=>{
      const ch=Number(card.dataset.channel),angle=preview[ch-1],on=!!(mask&(1<<(ch-1)));
      card.classList.toggle('selected',ch===selected);card.classList.toggle('energized',on);
      card.querySelector('.servo-preview-horn').setAttribute('transform',`rotate(${angle} 120 105)`);
      const pulse=on?s.servo?.pulse_us[ch-1]:null,commanded=finite(pulse)?angles.fromPulse(pulse):null;
      card.querySelector('.servo-command-horn').setAttribute('visibility',finite(commanded)?'visible':'hidden');
      if(finite(commanded))card.querySelector('.servo-command-horn').setAttribute('transform',`rotate(${commanded} 120 105)`);
      card.querySelector('.servo-draft').textContent=angleText(angle);
      card.querySelector('.servo-command-value').textContent=finite(commanded)?'MCU '+angleText(commanded)+' · '+pulse+' µs':'PREVIEW ONLY';
      card.querySelector('button').setAttribute('aria-pressed',ch===selected?'true':'false');
    });
    const angle=preview[selected-1];
    $('servo-selected').textContent='PCB PWM '+selected;
    $('servo-enable').textContent='Enable PWM '+selected;
    if(syncInput)$('servo-angle').value=angle;
    $('servo-preview-value').textContent=angleText(angle);
    $('servo-preview-pulse').textContent=angles.toPulse(angle)+' µs';
    $('servo-angle-needle').setAttribute('transform',`rotate(${angle} 150 130)`);
    document.querySelectorAll('[data-servo-angle]').forEach(button=>{
      const value=Number(button.dataset.servoAngle);
      button.setAttribute('aria-pressed',value===angle?'true':'false');
      button.disabled=busy||!!active()&&!canMove(angles.toPulse(value));
    });
    $('servo-move').disabled=!canMove();
  }
  $('servo-models').addEventListener('click',event=>{
    const button=event.target.closest('[data-servo-select]');if(button)select(Number(button.dataset.servoSelect));
  });
  $('servo-angle-options').addEventListener('click',event=>{
    const button=event.target.closest('[data-servo-angle]');if(button&&!button.disabled)void choose(button.dataset.servoAngle);
  });
  $('servo-angle').addEventListener('input',()=>draft($('servo-angle').value,false));
  $('servo-angle-form').addEventListener('submit',event=>{event.preventDefault();void choose($('servo-angle').value);});
  $('servo-confirm').addEventListener('input',()=>render(state));
  $('servo-enable').onclick=async()=>{
    if(busy||$('servo-enable').disabled)return;
    halted=false;busy=true;preview[selected-1]=angles.fromPulse(angles.enablePulse);paint();
    try{await act('servo',{operation:'enable',channel:selected,minimum_us:angles.minimumPulse,maximum_us:angles.maximumPulse,
      confirmed:$('servo-confirm').checked,generation:state.generation,control_epoch:state.servo_control_epoch});}
    finally{busy=false;render(state);}
  };
  async function stop(){halted=true;await act('servo-stop');render(state);}
  $('servo-stop').onclick=stop;
  function leaveStop(){
    halted=true;
    if(capable()&&(active()||state.pending?.startsWith('servo ')||busy)){
      fetch('/api/servo-stop',{method:'POST',headers:{'X-Atlas-Token':token,'Content-Type':'application/json'},body:'{}',keepalive:true}).catch(()=>{});
    }
  }
  window.addEventListener('pagehide',leaveStop);
  document.addEventListener('visibilitychange',()=>{if(document.hidden)leaveStop();});
  function render(v){
    if(!v){paint();return;}
    if(generation!==v.generation){generation=v.generation;$('servo-confirm').checked=false;observedSeq=null;}
    const s=v.status,valid=usable(s),cap=capable(),on=valid&&!!active(),policyOk=policy();
    if(s&&s.seq!==observedSeq){observedSeq=s.seq;observedAt=performance.now();}
    const pwm=valid?railValue(s,1):null;
    const voltageOk=finite(pwm)&&pwm>0&&pwm<=8.55&&age(s,s.power.t)<=100;
    $('servo-enable').disabled=!cap||!policyOk||!valid||!voltageOk||v.age_ms>1000||!s?.servo?.ready||on||!!v.pending||v.batch.length>0||v.updating||!$('servo-confirm').checked||busy;
    $('servo-stop').disabled=!cap||v.updating;
    $('servo-active').textContent=on?'PWM ACTIVE':cap&&!valid?'STATE UNKNOWN':'OUTPUT OFF';
    $('servo-active').className='badge '+(on?'live':'');
    $('servo-supply').textContent=number(pwm,3)+' V';
    $('servo-system').textContent=number(valid?railValue(s,0):null,3)+' V';
    $('servo-gate').textContent=!cap?(v.mode==='demo'?'SIMULATED PREVIEW · No hardware commands.':'ServoBench required. Angle selection remains available for local preview.'):
      !policyOk?'Update to ServoBench 1.2.5 for direct position moves. Stop remains available.':
      !valid?'Need fresh telemetry. Stop all PWM remains available.':!s.servo.ready||!voltageOk?
      `Output blocked · PWM ${number(pwm,3)} V. Need a usable, nonzero PWM-supply ADC reading at or below 8.55 V and a healthy output monitor.`:
      on?'Choose an angle below, or enter an angle and press Move. Each selection sends one target.':
      'Confirm free movement and enable PWM, then choose an angle. While output is off, angles are previews only.';
    $('servo-countdown').textContent=on?number(s.servo.remaining_ms/1000,1)+' s until idle cutoff · maximum session 30 s':
      (s?.servo?.stop_reason?stopReasons[s.servo.stop_reason]+(s.servo.stop_reason===4&&finite(s.servo.stop_pwm_mv)?' · last ADC sample '+number(s.servo.stop_pwm_mv/1000,3)+' V':'')+' · ':'')+'3 s idle cutoff · enable again for another move';
    if(on)selected=Math.log2(active())+1;
    paint(document.activeElement!==$('servo-angle'));
  }
  paint();
  return {render,leaving:(from,to)=>{if(from==='servos'&&to!=='servos')leaveStop();}};
})();
