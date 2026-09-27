/* A green radio LED or successful UART write alone never claims a working Atlas link. */
'use strict';
(() => {
  const uid = words => words?.some(Boolean) ? words.map(w=>w.toString(16).padStart(8,'0').toUpperCase()).join('-') : '—';
  function presentation(v, usable) {
    const s=v.status, r=s?.radio;
    const supported=v.hello?.radio_link===1 && r?.connected!==undefined;
    const live=v.mode==='live' && usable && ((s.ms-s.owner_ms)>>>0)<=500;
    const ready=live && supported && !!(s.attempted&128) && s.init[9]===0 && !r.command;
    const connected=ready && r.connected===1 && r.ack_age_ms+((s.ms-s.owner_ms)>>>0)+(v.age_ms||0)<6000;
    const label=!live?'Status unavailable':!supported?'Firmware update required':!ready?'Transport unavailable':
      connected?'Connected · peer replied':r.waiting?'Checking connection…':r.monitoring&&r.timeouts?'No peer response':'Not connected';
    let result='No test run yet.';
    if(supported) result=[
      'No test run yet.',
      `Test #${r.test_sequence}: waiting for peer response…`,
      `Test #${r.test_sequence}: reply from ${uid(r.test_peer)} in ${r.test_rtt_ms} ms.`,
      `Test #${r.test_sequence}: no reply within 2 seconds. Check both radios and peer firmware.`,
      `Test #${r.test_sequence}: UART transmission failed. No radio delivery confirmed.`,
      `Test #${r.test_sequence}: cancelled before a peer reply.`
    ][r.test];
    if(!live && supported && r.test) result='Last recorded result · '+result;
    const enabled=ready && !v.pending && !v.batch?.length && !v.updating && !v.blocked;
    return {label,result,connected,enabled,supported,live,ready};
  }
  function render(v,usable) {
    const m=presentation(v,usable),s=v.status,r=s?.radio;
    const badge=document.getElementById('radio-status');
    badge.textContent=m.label; badge.className='badge '+(m.connected?'live':m.live?'error':'');
    document.getElementById('radio-summary').textContent=m.connected?
      `Atlas ${uid(r.peer)} · ${r.rtt_ms} ms round trip`:
      m.ready?'Ready to test. The other Atlas must also run firmware 1.4.0 or later.':
      'Connect a board with current firmware to verify a peer.';
    document.getElementById('radio-test-result').textContent=m.result;
    for(const id of ['radio-connect','radio-test','radio-stop']) document.getElementById(id).disabled=!m.enabled;
    document.getElementById('radio-connect').disabled||=!!r?.monitoring;
    document.getElementById('radio-stop').disabled||=!r?.monitoring && !r?.connected;
    document.getElementById('radio-probe').disabled||=!!(s?.attempted&128);
    document.getElementById('radio-identity').disabled||=!!r?.monitoring || !!v.remote?.streaming || !!s?.stabilization?.enabled || !!s?.gpio?.pwm;
    details('radio-details',[
      ['Transport',m.live?(s.attempted&128?(s.init[9]?'Initialization failed':'Initialized'):'Not initialized'):'—'],
      ['Serial format',m.supported?`${r.baud} baud · 8N1`:'Update both boards to firmware 1.4.0'],
      ['Monitoring',m.live&&m.supported?(r.monitoring?'Every 3 seconds':'Stopped; responds to peer tests'):'—'],
      ['Last responding Atlas',m.supported?uid(r.peer):'—'],
      ['Last round trip',m.live&&m.supported&&r.ack_age_ms!==0xFFFFFFFF?`${r.rtt_ms} ms · ${r.ack_age_ms} ms ago`:'—'],
      ['Tests sent / acknowledged',m.supported?`${r.sent} / ${r.acknowledgements}`:'—'],
      ['Peer tests received / replies sent',m.supported?`${r.received} / ${r.replies}`:'—'],
      ['Timeouts / transmit errors',m.supported?`${r.timeouts} / ${r.tx_errors}`:'—'],
      ['UART received / errors / dropped',m.live?`${r.rx} bytes / ${r.uart_errors??'—'} / ${r.dropped??'—'}`:'—'],
      ['Invalid frames',m.supported?r.invalid:'—'],
      ['Mode',m.live?(r.command?'Local command mode':'Transparent data'):'—'],
      ['RSSI / SNR','Not reported by this protocol']
    ]);
  }
  window.AtlasRadio={render,presentation};
})();
