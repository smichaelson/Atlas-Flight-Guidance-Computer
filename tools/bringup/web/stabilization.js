/* Atlas owns the autonomous loop. This page sends deliberate settings only. */
'use strict';
window.AtlasStabilization=(()=>{
  const channels=[7,8,1,2],azimuths=[45,135,225,315];
  const labels=['Calibration needed','Disabled','Waiting for SW2','Waiting for sensor / power','Stabilizing','Stopped · cycle SW2','Saving settings'];
  const reasons={1:'Stopped by operator',4:'PWM supply interlock',7:'IMU data invalid or stale',9:'Settings were not saved'};
  const faults={1:'IMU read failed',2:'IMU sample older than 100 ms',3:'Rotation exceeded 500°/s',
    4:'Acceleration outside 0.1–2 g',5:'Gravity vector invalid',6:'Gravity correction unavailable',
    7:'IMU settling restarted',8:'Servo geometry invalid',9:'Servo destination invalid',10:'Nonfinite IMU data'};
  let busy=false,generation=null,dirty=false;
  const dirs=channels.map(ch=>$('stab-reverse-'+ch));
  function capable(){return state?.mode==='live'&&state.hello?.stabilization===true&&state.hello.stabilization_layout===1;}
  function writable(){
    const s=state?.status;
    return capable()&&usable(s)&&state.age_ms<=1000&&!busy&&!state.pending&&!s.pending_id&&!state.batch.length&&
      !state.updating&&!s.stabilization?.save_busy&&!s.gpio.switch&&!s.gpio.pwm&&!!s.sd.card&&$('stab-confirm').checked;
  }
  async function change(operation){
    if(operation!=='off'&&!writable())return;
    if(operation==='off'&&!capable())return;
    const context={operation,generation:state.generation,control_epoch:state.servo_control_epoch,
      confirmed:$('stab-confirm').checked,reverse_mask:dirs.reduce((m,el,i)=>m|(el.checked?1<<i:0),0)};
    busy=true;render(state);
    try{if(await act('stabilization',context))dirty=false;}
    finally{busy=false;render(state);}
  }
  $('stab-toggle').onchange=()=>{const enabled=$('stab-toggle').checked;render(state);void change(enabled?'on':'off');};
  $('stab-calibrate').onclick=()=>change('calibrate');
  $('stab-save-directions').onclick=()=>change('directions');
  $('stab-stop').onclick=()=>act('servo-stop');
  $('stab-confirm').onchange=()=>render(state);
  dirs.forEach(el=>el.onchange=()=>{dirty=true;render(state);});
  function render(v){
    if(!v)return;
    const s=v.status,st=s?.stabilization,valid=usable(s),cap=capable();
    if(generation!==v.generation){generation=v.generation;$('stab-confirm').checked=false;dirty=false;}
    const enabled=!!st?.enabled,can=writable();
    const reason=st?.reason===7?(faults[st.fault_detail]||reasons[7]):reasons[st?.reason];
    $('stab-toggle').checked=enabled;
    $('stab-toggle').disabled=enabled?(!cap||v.updating):(!can||!st?.calibrated||!st?.saved);
    $('stab-calibrate').disabled=!can||!st?.calibration_ready;
    $('stab-save-directions').disabled=!can||!st?.calibrated||!dirty;
    $('stab-stop').disabled=!cap||v.updating;
    dirs.forEach((el,i)=>{if(!dirty)el.checked=!!(st?.reverse_mask&(1<<i));el.disabled=!can;});
    $('stab-state').textContent=!cap?(v.mode==='demo'?'DEMO · controls disabled':'ServoBench 1.3.0 required'):!valid?'State unknown':labels[st?.state]||'Awaiting state';
    $('stab-state').className='badge '+(valid&&st?.active?'live':'');
    $('stab-switch').textContent=valid?(s.gpio.switch?'ON':'OFF'):'—';
    $('stab-saved').textContent=!valid?'—':st?.save_busy?'Saving…':st?.saved?'Verified on SD card':'No verified settings';
    $('stab-calibration').textContent=!valid?'—':st?.calibrated?'Upright saved':st?.calibration_ready?'Ready to calibrate':'Hold still · USB-C up';
    $('stab-detail').textContent=!cap?'Build and install ServoBench 1.3.0 to configure stabilization.':!valid?
      'Telemetry unavailable. Saved stabilization can keep running on Atlas. Use SW2 to stop it.':
      st?.save_busy?'Keep power and the SD card connected while settings are saved and read back.':
      st?.reason===9?'Save failed. Outputs are stopped; inspect the SD card and check settings again.':
      st?.active?'Atlas is following gravity on PWM 7, 8, 1 and 2. USB and this page are not needed for control.':
      st?.enabled?(reason?reason+'. ':'')+'Switch SW2 OFF, then ON to start when IMU and power are ready.':
      'With SW2 OFF, calibrate once at the upright resting position, verify each servo direction, then enable the saved toggle.';
    channels.forEach((ch,i)=>{
      const pulse=valid&&s.gpio.pwm&(1<<(ch-1))?s.servo?.pulse_us[ch-1]:0;
      const angle=pulse?(pulse-1500)/10:0;
      const hornAngle=st?.reverse_mask&(1<<i)?-angle:angle;
      $('stab-angle-'+ch).textContent=pulse?number(angle,1)+'°':'PWM off';
      $('stab-horn-'+ch).setAttribute('transform',`rotate(${hornAngle} 50 38)`);
      $('stab-limit-'+ch).textContent=!valid?'No telemetry':st?.singular&(1<<i)?'Gravity along shaft · holding':st?.limited&(1<<i)?'Travel limit · ±50°':azimuths[i]+'° clockwise';
    });
  }
  return {render};
})();
