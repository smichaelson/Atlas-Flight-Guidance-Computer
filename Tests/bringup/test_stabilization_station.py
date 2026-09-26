"""Stabilization wire/HTTP authorization and malformed telemetry tests; no serial access."""
import copy
import time
import unittest
from test_servo_station import frames, Serial, gs
from protocol import validate, valid_command

class StabilizationTests(unittest.TestCase):
    def setUp(self):
        self.station=gs.Station(); self.h,self.s=frames()
        self.h.update(stabilization=True,stabilization_layout=1,stabilization_storage='sd')
        self.s['stabilization']=dict(state=1,enabled=0,calibrated=1,calibration_ready=1,
            active=0,saved=1,save_busy=0,reason=0,limited=0,singular=0,reverse_mask=0,up_mg=[0,-1000,0])
        self.s['gpio']['switch']=0; self.s['sd']['card']=1; self.s['count'][1]=100
        self.s['sample_status'][1]=0; self.s['lsm']['t']=self.s['ms']
        self.station.mode='live';self.station.serial=Serial();self.station.confirmed=True
        self.station.ingest(validate(self.h),time.monotonic());self.publish()

    def publish(self): self.station.ingest(validate(self.s),time.monotonic())
    def action(self,operation='on',**extra):
        self.station.action('stabilization',dict(operation=operation,generation=self.station.generation,
            control_epoch=self.station.servo_control_epoch,confirmed=True,reverse_mask=0,**extra))

    def test_exact_commands(self):
        for verb in ('stabilize on','stabilize off','stabilize calibrate 0','stabilize directions 15'):
            self.assertTrue(valid_command(verb))
        for verb in ('stabilize on 1','stabilize','stabilize directions -1','stabilize calibrate 16','stabilize calibrate 0\n'):
            self.assertFalse(valid_command(verb))

    def test_on_calibrate_directions_are_deliberate(self):
        for op in ('on','calibrate','directions'):
            self.setUp();self.action(op)
            tail=' 0' if op in ('calibrate','directions') else ''
            self.assertEqual(self.station.serial.writes,[f'1 stabilize {op}{tail}\n'.encode()])

    def test_off_bypasses_stale_and_pending_but_fences_older_http(self):
        old=dict(operation='on',generation=self.station.generation,control_epoch=self.station.servo_control_epoch,confirmed=True)
        self.action('on'); self.station.session.blocked='uncertain';self.station.session.received_at=0
        self.station.action('stabilization',dict(operation='off'))
        self.assertEqual(self.station.serial.writes[-1],b'2 stabilize off\n')
        with self.assertRaises(ValueError): self.station.action('stabilization',old)

    def test_enable_checks_power_sensor_card_switch_and_calibration(self):
        for section,key,value in [('gpio','switch',1),('sd','card',0),('stabilization','saved',0),
                ('stabilization','calibrated',0),('stabilization','save_busy',1),('power','status',6),
                ('power','valid',0),('tasks','fault',1),('lsm','t',0)]:
            self.setUp();self.s[section][key]=value;self.publish()
            with self.assertRaises(ValueError):self.action()
            self.assertEqual(self.station.serial.writes,[])

    def test_profile_capability_and_setup_required(self):
        self.station.session.hello.pop('stabilization')
        with self.assertRaises(ValueError):self.action()
        self.setUp()
        with self.assertRaises(ValueError):self.station.action('stabilization',dict(operation='on',generation=0,control_epoch=0,confirmed=False))
        with self.assertRaises(ValueError):self.station.action('command',dict(verb='stabilize on',action_confirmed=True))
        self.station.mode='demo'
        with self.assertRaises(ValueError):self.action()

    def active(self):
        self.s['stabilization'].update(state=4,enabled=1,active=1)
        self.s['gpio'].update(switch=1,pwm=0xC3)
        self.s['servo'].update(ready=0,min_us=1000,max_us=2000,pulse_us=[1500,1500,0,0,0,0,1500,1500])

    def test_four_channel_telemetry_requires_exact_active_state(self):
        self.active();validate(self.s)
        for section,key,value in [('stabilization','enabled',0),('stabilization','active',0),
                ('stabilization','saved',0),('stabilization','state',3),('stabilization','limited',16),
                ('stabilization','up_mg',[1002,0,0]),('gpio','switch',0),('gpio','pwm',0xC7),
                ('servo','target_us',1500),('servo','remaining_ms',3000)]:
            bad=copy.deepcopy(self.s);bad[section][key]=value
            with self.assertRaises(ValueError):validate(bad)

    def test_fault_detail_is_optional_and_bounded(self):
        validate(self.s)  # Earlier firmware does not include this field.
        for value in range(11):
            self.s['stabilization']['fault_detail']=value
            validate(self.s)
        for value in (-1,11,True,'1',None):
            self.s['stabilization']['fault_detail']=value
            with self.assertRaises(ValueError):validate(self.s)

    def test_manual_dfu_and_sensor_maintenance_blocked_while_enabled(self):
        self.s['stabilization']['enabled']=1;self.publish()
        for verb in ('servo enable 1 1000 2000','probe lsm','bootloader '+' '.join(map(str,self.h['uid']))):
            with self.assertRaises(ValueError):self.station.session.request(verb,time.monotonic(),True)
        self.station.session.request('status',time.monotonic(),True)

    def test_calibration_and_reversal_require_valid_state(self):
        self.s['stabilization']['calibration_ready']=0;self.publish()
        with self.assertRaises(ValueError):self.action('calibrate')
        self.s['stabilization']['calibrated']=0;self.publish()
        with self.assertRaises(ValueError):self.action('directions')
        for value in (True,-1,16,'1'):
            with self.assertRaises(ValueError):self.station.action('stabilization',dict(operation='directions',generation=0,control_epoch=self.station.servo_control_epoch,confirmed=True,reverse_mask=value))

if __name__=='__main__':unittest.main(verbosity=2)
