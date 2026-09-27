/* Remote Atlas is read-only. Every value comes from the separately validated RF snapshot. */
'use strict';
(() => {
  const uid=a=>a?.some(Boolean)?a.map(n=>n.toString(16).padStart(8,'0').toUpperCase()).join('-'):'—';
  const pct=(n,d)=>d?100*n/d:null;
  const elapsed=(a,b)=>(a-b)>>>0;
  const statusNames=['OK','NULL','ARGUMENT','BUSY','NOT READY','TIMEOUT','IO ERROR','IDENTITY','CRC','PROTOCOL','NACK','OVERFLOW','UNSUPPORTED','STATE'];
  function presentation(v,usable){
    const r=v.remote,d=r?.data;
    const supported=v.hello?.remote_telemetry===1;
    const local=v.mode==='live'&&usable&&supported&&!!v.status&&elapsed(v.status.ms,v.status.owner_ms)<=500;
    const envelope=local&&!!r&&Number.isFinite(v.remote_age_ms)&&v.remote_age_ms>=0&&v.remote_age_ms<=2500&&elapsed(r.ms,r.owner_ms)<=500;
    const receivedAge=r?elapsed(r.ms,r.received_ms)+(v.remote_age_ms||0):null;
    const fresh=envelope&&r.available===1&&!!d&&receivedAge<3500;
    const label=!supported?'Firmware 1.5.0 required':!local?'Gateway unavailable':!r?'Waiting for radio telemetry':
      !envelope?'Gateway telemetry stale':fresh?'Receiving · remote Atlas':d?'Remote data stale':'Waiting for remote Atlas';
    const extra=fresh?receivedAge+r.assembly_ms:Infinity;
    const ageAtCapture=stamp=>d?elapsed(d.ms,stamp):null;
    const recent=(stamp,limit)=>fresh&&ageAtCapture(stamp)+extra<=limit;
    const sensor=(index,key)=>fresh&&!!(d.attempted&(1<<index))&&d.init[index]===0&&d.count[index]>0&&
      [0,4].includes(d.sample_status[index])&&recent(d[key].t,3000);
    const adc=fresh&&d.power.available&&d.power.status===0&&d.power.count>0&&recent(d.power.t,3000);
    const gnss=fresh&&!!(d.attempted&32)&&d.init[6]===0&&d.init[7]===0&&d.gnss.frames>0&&recent(d.gnss.t,4000);
    const fix=gnss&&[3,4].includes(d.gnss.fix)&&!!(d.gnss.flags&1)&&Number.isFinite(d.gnss.lat_e7)&&
      Number.isFinite(d.gnss.lon_e7)&&Math.abs(d.gnss.lat_e7)<=900000000&&Math.abs(d.gnss.lon_e7)<=1800000000;
    return {r,d,supported,local,envelope,receivedAge,fresh,label,extra,ageAtCapture,recent,sensor,adc,gnss,fix,
      loss:r?pct(r.stats.missing_packets,r.stats.expected_packets):null,
      crc:r?pct(r.stats.crc_errors,r.stats.rx_packets+r.stats.crc_errors):null};
  }
  const vector=(values,scale,unit)=>values?.every(Number.isFinite)?values.map(n=>number(n/scale,3)).join(' / ')+' '+unit:'—';
  function render(v,usable){
    const m=presentation(v,usable),r=m.r,d=m.d,c=r?.stats;
    const badge=$('remote-status');badge.textContent=m.label;badge.className='badge '+(m.fresh?'live':m.local?'error':'');
    $('remote-identity').textContent=r?.peer.some(Boolean)?`Remote ${uid(r.peer)} · via ${v.port||'USB'} / ${uid(r.gateway)}`:'Power both Atlas boards. The remote board can run on battery alone.';
    $('remote-source').textContent=m.fresh?`${d.version} · ${d.profile==='servo_bench'?'ServoBench':'Bringup'} · snapshot #${r.sequence} · source uptime ${number(d.ms/1000,1)} s`:
      m.d?'Last received identity retained; measurements are hidden until fresh data arrives.':'Automatic, read-only telemetry · both boards require firmware 1.5.0.';
    metric('remote-age',m.envelope&&d?m.receivedAge:null,'ms',0);
    metric('remote-rate',m.envelope?c.payload_bps:null,'B/s',0);
    metric('remote-loss',m.envelope?m.loss:null,'%',2);
    metric('remote-crc',m.envelope?m.crc:null,'%',2);
    $('remote-loss-detail').textContent=c?`${c.missing_packets} missing / ${c.expected_packets} expected packets`:'Awaiting completed batches';
    $('remote-crc-detail').textContent=c?`${c.crc_errors} rejected / ${c.rx_packets+c.crc_errors} framed packets`:'CRC-32 checked on every packet';
    const enabled=m.envelope&&!v.pending&&!v.batch?.length&&!v.updating&&!v.blocked;
    $('remote-pause').disabled=!enabled||!r.streaming;
    $('remote-resume').disabled=!enabled||!!r.streaming||!r.tx_ready;
    $('remote-local-stream').textContent=!m.envelope?'Awaiting gateway telemetry':!r.tx_ready?'Gateway boot identifier unavailable; its broadcast is disabled':
      r.streaming?'This USB-connected Atlas also broadcasts its own readings.':'This USB-connected Atlas has paused its own broadcast; reception continues.';
    const rows=[];
    if(d){
      for(const [label,index,key,value] of [
        ['ADXL375 acceleration',0,'adxl',vector(d.adxl.mg,1000,'g')],
        ['LSM6 acceleration',1,'lsm',vector(d.lsm.mg,1000,'g')],
        ['LSM6 angular rate',1,'lsm',vector(d.lsm.mdps,1000,'°/s')],
        ['LSM6 temperature',1,'lsm',number(d.lsm.temp_cc===null?null:d.lsm.temp_cc/100,2)+' °C'],
        ['MMC5983 magnetic field',2,'mmc',vector(d.mmc.nt,1000,'µT')],
        ['MS5611 pressure / temperature',3,'baro',`${number(d.baro.pa===null?null:d.baro.pa/100,2)} hPa / ${number(d.baro.temp_cc===null?null:d.baro.temp_cc/100,2)} °C`]
      ]){
        const ok=m.sensor(index,key),state=!m.fresh?'STALE':!(d.attempted&(1<<index))?'NOT STARTED':d.init[index]?statusNames[d.init[index]]:ok?'CURRENT':'NO FRESH SAMPLE';
        rows.push([label,state,`${m.ageAtCapture(d[key].t)} ms`,ok?value:'—']);
      }
      for(const [i,label,field,scale,unit] of [[0,'BNO085 acceleration','accel_mm_s2',1000,'m/s²'],[1,'BNO085 angular rate','gyro_mrad_s',1000,'rad/s'],[2,'BNO085 magnetic field','mag_nt',1000,'µT'],[3,'BNO085 quaternion W/X/Y/Z','q_ppm',1e6,'']]){
        const ok=m.fresh&&d.init[4]===0&&d.init[5]===0&&d.bno.count[i]>0&&m.recent(d.bno.t[i],3000);
        rows.push([label,!m.fresh?'STALE':d.init[4]?statusNames[d.init[4]]:ok?'CURRENT · accuracy '+d.bno.accuracy[i]:'NO FRESH SAMPLE',`${m.ageAtCapture(d.bno.t[i])} ms`,ok?vector(d.bno[field],scale,unit):'—']);
      }
    }
    $('remote-sensors').innerHTML=rows.length?rows.map(a=>'<tr>'+a.map(x=>'<td>'+esc(x)+'</td>').join('')+'</tr>').join(''):'<tr><td colspan="4">Waiting for a complete, checked remote snapshot.</td></tr>';
    const g=m.gnss?d.gnss:null;
    details('remote-gnss',[
      ['Position',m.fix?'3D fix':g?'Receiver reporting; no valid 3D fix':'No fresh receiver data'],
      ['Latitude',m.fix?number(g.lat_e7/1e7,7)+'°':'—'],['Longitude',m.fix?number(g.lon_e7/1e7,7)+'°':'—'],
      ['MSL altitude',m.fix&&Number.isFinite(g.h_msl_mm)?number(g.h_msl_mm/1000,3)+' m':'—'],
      ['Horizontal accuracy',m.fix?number(g.hacc_mm/1000,3)+' m':'—'],['Satellites',g?g.sv:'—'],
      ['GNSS time of week',g?g.tow_ms+' ms':'—'],['PPS count / period',g?`${g.pps_count} / ${g.pps_us} µs`:'—'],
      ['NAV frames / checksum errors',g?`${g.frames} / ${g.crc_errors}`:'—'],
      ['UART errors / dropped bytes',g?`${g.uart_errors} / ${g.dropped}`:'—']]);
    const p=d?.power;
    $('remote-rails').innerHTML=['3V3_SYS','8V4_PWM','5V_SYS','VIN_PROT','ARMED FEED','CONTINUITY 1','CONTINUITY 2','CONTINUITY 3','CONTINUITY 4','CONTINUITY 5'].map((name,i)=>{
      const ok=m.adc&&!!(p.valid&(1<<i));
      return `<div class="rail"><small>${name}</small><strong>${number(ok?p.mv[i]/1000:null,3)} <small>V</small></strong><em>RAW ${ok?p.raw[i]:'—'}</em></div>`;
    }).join('');
    $('remote-adc-detail').textContent=m.adc?`VDDA ${p.vdda_mv} mV · MCU ${p.temp_c} °C · ${p.count} scans · ${p.adc_errors} ADC errors`:'No fresh valid ADC snapshot.';
    const health=m.fresh?d:null;
    details('remote-health',[
      ['Sensor startup',health?(health.startup?'Completed; see individual sensor states':'In progress'):'—'],
      ['Supervisor fault',health?health.fault:'—'],['Switch SW2',health?(health.gpio.switch?'ON':'OFF'):'—'],
      ['PWM enabled mask',health?'0x'+health.gpio.pwm.toString(16).toUpperCase():'—'],
      ['Stabilization',health?(health.stabilization.active?'Active':health.stabilization.enabled?'Enabled / waiting':'Off'):'—'],
      ['SD card / mounted',health?`${health.sd.card?'Present':'Absent'} / ${health.sd.mounted?'Yes':'No'}`:'—'],
      ['Power events / storage errors',health?`${health.power.power_events} / ${health.sd.errors}`:'—'],
      ['Direct sensor errors',health?health.errors.join(' / '):'—'],
      ['BNO IO / protocol errors',health?`${health.bno.io_errors} / ${health.bno.protocol_errors}`:'—'],
      ['Reset flags',health?'0x'+health.power.reset_flags.toString(16).toUpperCase():'—']]);
    details('remote-transport',[
      ['Payload / received packet bytes',m.envelope?`${c.payload_bps} / ${c.rx_bps} B/s`:'—'],
      ['Received packets',m.envelope?`${c.rx_pps} packets/s · ${c.rx_packets} total`:'—'],
      ['Complete / lost snapshots',c?`${c.good_batches} / ${c.lost_batches}`:'—'],
      ['Repaired snapshots / packets',c?`${c.recovered_batches} / ${c.recovered_packets}`:'—'],
      ['Whole-snapshot CRC failures',c?c.batch_crc_errors:'—'],
      ['Duplicates / old packets',c?`${c.duplicates} / ${c.old_packets}`:'—'],
      ['Unsupported headers / other peers',c?`${c.header_errors} / ${c.foreign_packets}`:'—'],
      ['Observed peer sessions',c?c.sessions:'—'],['Peer boot ID',r?uid(r.boot):'—'],
      ['Last assembly duration',d?r.assembly_ms+' ms':'—'],
      ['Local transmitted packets / bytes',c?`${c.tx_packets} / ${c.tx_bytes}`:'—'],
      ['Local UART TX errors / abandoned batches',c?`${c.tx_errors} / ${c.tx_aborted}`:'—'],
      ['Local UART errors / dropped bytes',r?`${r.uart_errors} / ${r.uart_dropped}`:'—'],
      ['Host rejected remote records',v.remote_decoder_errors??0],
      ['RF RSSI / bit error rate','Not exposed by this protocol']]);
  }
  window.AtlasRemote={presentation,render};
})();
