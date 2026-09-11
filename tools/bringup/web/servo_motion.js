/* KST nominal commands: -50 / 0 / +50 degrees = 1000 / 1500 / 2000 us.
   This conversion is not measured shaft position. */
'use strict';
window.AtlasServoAngles=Object.freeze({
  minimum:-50,maximum:50,minimumPulse:1000,maximumPulse:2000,enablePulse:1520,
  toPulse(angle){return Number.isInteger(angle)&&angle>=-50&&angle<=50?1500+angle*10:null;},
  fromPulse(pulse){return Number.isFinite(pulse)?(pulse-1500)/10:null;}
});
