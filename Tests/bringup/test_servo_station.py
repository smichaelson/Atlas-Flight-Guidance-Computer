"""Inert ServoBench protocol/HTTP-owner tests, without a COM port or device."""
import copy
from pathlib import Path
import sys
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/bringup'))
import demo
import ground_station as gs
from protocol import Session, validate, valid_command

def frames():
    h=demo.hello()
    h.update(profile='servo_bench',version='1.2.5',pwm_pyro_inhibited=False,pyro_inhibited=True,servo_test=True,software_dfu=True,servo_pwm_max_mv=8550,servo_layout=1,servo_direct=True,servo_adc_samples=16)
    s=demo.status()
    s.update(profile='servo_bench',pyro_inhibited=True)
    s['gpio']['pwm']=s['gpio']['armed']=0
    s['power'].update(status=0,available=True,count=1,valid=0x3FF,t=s['ms'])
    s['power']['mv'][:5]=[3360,7400,5000,15100,0]
    s['servo']=dict(ready=1,remaining_ms=0,stop_reason=0,min_us=0,max_us=0,pulse_us=[0]*8,target_us=0,stop_pwm_mv=0)
    return h,s

class Serial:
    def __init__(self): self.writes=[];self.dtr=True
    def write(self,data): self.writes.append(data);return len(data)
    def close(self): pass

class ServoTests(unittest.TestCase):
    def setUp(self):
        self.station=gs.Station();self.h,self.s=frames();now=time.monotonic()
        self.station.mode='live';self.station.serial=Serial();self.station.confirmed=True
        self.station.ingest(self.h,now);self.station.ingest(self.s,now)
        self.enable=dict(operation='enable',channel=1,minimum_us=1320,maximum_us=1720,confirmed=True)

    def servo(self, body):
        self.station.action('servo',dict(generation=self.station.generation,
            control_epoch=self.station.servo_control_epoch,**body))

    def active(self):
        self.s['gpio']['pwm']=1
        self.s['servo'].update(min_us=1320,max_us=1720,remaining_ms=3000,pulse_us=[1520]+[0]*7,target_us=1520)
        self.station.session.pending=None
        self.station.ingest(self.s,time.monotonic())

    def test_explicit_enable_then_bounded_position(self):
        self.servo(self.enable)
        self.assertEqual(self.station.serial.writes,[b'1 servo enable 1 1320 1720\n'])
        self.active()
        self.servo(dict(operation='set',channel=1,pulse_us=1600))
        self.assertEqual(self.station.serial.writes[-1],b'2 servo set 1 1600\n')

    def test_missing_or_contradictory_capability_rejected(self):
        for key,value in (('servo_test',False),('pyro_inhibited',False),('pwm_pyro_inhibited',True),('profile','normal')):
            h=copy.deepcopy(self.h);h[key]=value
            with self.assertRaises(ValueError): validate(h)

    def test_ordinary_bringup_cannot_enable(self):
        session=Session();session.accept(demo.hello(),1);session.accept(demo.status(),1)
        with self.assertRaises(ValueError): session.request('servo enable 1 1320 1720',1,True)

    def test_malformed_enabled_state_fails_closed(self):
        self.active()
        for change in (lambda s:s['gpio'].update(pwm=3), lambda s:s['gpio'].update(armed=1),
                       lambda s:s['servo'].update(pulse_us=[1721]+[0]*7),
                       lambda s:s['servo'].update(pulse_us=[1520,1500]+[0]*6),
                       lambda s:s['servo'].update(remaining_ms=3001),
                       lambda s:s.update(pyro_inhibited=False)):
            s=copy.deepcopy(self.s);change(s)
            with self.assertRaises(ValueError): validate(s)

    def test_specific_setup_and_travel_bounds(self):
        for key,value in (('confirmed',False),('channel',True),('channel',9),
                          ('minimum_us',1520),('maximum_us',2101)):
            body=dict(self.enable);body[key]=value
            with self.assertRaises(ValueError): self.servo(body)
        self.assertFalse(self.station.serial.writes)

    def test_adc_overvoltage_independent_of_dmm_form(self):
        self.s['power']['mv'][1]=8551
        self.station.ingest(self.s,time.monotonic())
        self.enable['pwm_mv']=7400 # An old client's meter field cannot override the ADC.
        with self.assertRaises(ValueError): self.servo(self.enable)
        self.assertFalse(self.station.serial.writes)

    def test_new_voltage_ceiling_without_unrelated_rail_or_meter_gates(self):
        for pwm in (1,4799,8301,8420,8550):
            with self.subTest(pwm=pwm):
                self.setUp()
                self.s['power']['mv'][:5]=[4000,pwm,0,0,12000]
                self.s['power']['valid']=2
                self.station.ingest(self.s,time.monotonic())
                self.servo(self.enable)
                self.assertEqual(self.station.serial.writes,[b'1 servo enable 1 1320 1720\n'])

    def test_unusable_adc_still_refuses_even_if_firmware_ready_is_set(self):
        for change in (lambda p:p['mv'].__setitem__(1,0),lambda p:p.update(valid=0),
                       lambda p:p.update(valid=0x3FD),lambda p:p.update(status=6),
                       lambda p:p.update(available=False),lambda p:p.update(t=(p['t']-101)&0xFFFFFFFF)):
            self.setUp();change(self.s['power']);self.station.ingest(self.s,time.monotonic())
            with self.assertRaises(ValueError): self.servo(self.enable)
            self.assertFalse(self.station.serial.writes)

    def test_legacy_firmware_needs_update_but_stop_still_works(self):
        self.station.session.hello.pop('servo_pwm_max_mv')
        with self.assertRaisesRegex(ValueError,'1.2.5'): self.servo(self.enable)
        self.station.action('servo-stop',{})
        self.assertEqual(self.station.serial.writes,[b'1 servo stop\n'])

    def test_voltage_capability_schema_rejects_noninteger_values(self):
        for value in (True,8.55,'8550',None,0,30001):
            h=copy.deepcopy(self.h);h['servo_pwm_max_mv']=value
            with self.assertRaises(ValueError): validate(h)

    def test_old_host_sample_rejected_for_servo(self):
        self.station.session.received_at=time.monotonic()-1.2
        with self.assertRaises(ValueError): self.servo(self.enable)

    def test_stop_bypasses_stale_fault_pending_and_batch(self):
        self.servo(self.enable)
        self.station.session.blocked='uncertain command'
        self.station.session.received_at=0
        self.station.batch=['probe bno']
        self.station.action('servo-stop',{})
        self.assertEqual(self.station.serial.writes[-1],b'2 servo stop\n')
        self.assertFalse(self.station.batch)
        self.assertIsNone(self.station.servo_approved)
        self.assertEqual(self.station.session.blocked,'uncertain command')

    def test_motion_needs_current_channel_enable(self):
        self.active()
        with self.assertRaises(ValueError): self.servo(dict(operation='set',channel=1,pulse_us=1600))

    def test_active_servo_blocks_dfu_and_slow_operations(self):
        self.active()
        for verb in ('sd mount','probe gnss','march','bootloader '+' '.join(map(str,self.h['uid']))):
            with self.assertRaises(ValueError): self.station.session.request(verb,time.monotonic(),True)

    def test_generic_command_route_cannot_bypass_setup(self):
        with self.assertRaises(ValueError): self.station.action('command',dict(verb='servo enable 1 1320 1720',action_confirmed=True))

    def test_disconnect_survives_removed_port_line_error(self):
        class Removed:
            def __setattr__(self,name,value): raise OSError('device gone')
            def close(self): raise OSError('already removed')
        self.station.serial=Removed()
        self.station.disconnect()
        self.assertEqual(self.station.mode,'disconnected')
        self.assertIsNone(self.station.serial)

    def test_profile_change_latches_block(self):
        self.station.ingest(demo.status(),time.monotonic())
        self.assertIn('profile changed',self.station.session.blocked)

    def test_old_mapping_firmware_blocks_enable_but_stop_works(self):
        for key in ('servo_layout','servo_direct','servo_adc_samples'):
            self.setUp();self.station.session.hello.pop(key)
            with self.assertRaisesRegex(ValueError,'1.2.5'):self.servo(self.enable)
            self.station.action('servo-stop',{})
            self.assertEqual(self.station.serial.writes,[b'1 servo stop\n'])

    def test_delayed_http_enable_or_move_cannot_cross_stop(self):
        old=dict(self.enable,generation=self.station.generation,control_epoch=self.station.servo_control_epoch)
        self.station.action('servo-stop',{})
        with self.assertRaisesRegex(ValueError,'session changed'):self.station.action('servo',old)
        self.station.session.pending=None
        self.servo(self.enable);self.active()
        move=dict(operation='set',channel=1,pulse_us=1600,generation=self.station.generation,
                  control_epoch=self.station.servo_control_epoch)
        self.station.action('servo-stop',{})
        with self.assertRaisesRegex(ValueError,'session changed'):self.station.action('servo',move)
        self.assertEqual(sum(b'servo set' in b for b in self.station.serial.writes),0)

    def test_motion_capability_and_target_types(self):
        for key in ('servo_layout','servo_slew_us_s','servo_adc_samples'):
            for value in (True,None,0,'1'):
                h=copy.deepcopy(self.h);h[key]=value
                with self.assertRaises(ValueError):validate(h)
        for value in (True,None,-1,1520):
            s=copy.deepcopy(self.s);s['servo']['target_us']=value
            with self.assertRaises(ValueError):validate(s)

    def test_direct_capability_is_strict_boolean_and_required_for_moves(self):
        for value in (1,0,None,'true',[],{}):
            h=copy.deepcopy(self.h);h['servo_direct']=value
            with self.assertRaises(ValueError):validate(h)
        h=copy.deepcopy(self.h);h['servo_direct']=False
        validate(h) # Legacy telemetry remains viewable, with Stop available.
        self.station.session.hello=h
        with self.assertRaisesRegex(ValueError,'1.2.5'):self.servo(self.enable)
        self.station.action('servo-stop',{})
        self.assertEqual(self.station.serial.writes,[b'1 servo stop\n'])
        self.setUp();self.servo(self.enable);self.active()
        self.station.session.hello.pop('servo_direct')
        self.station.session.hello['servo_slew_us_s']=600
        with self.assertRaisesRegex(ValueError,'1.2.5'):
            self.servo(dict(operation='set',channel=1,pulse_us=1600))
        self.assertEqual(len(self.station.serial.writes),1)

    def test_full_nominal_angle_range_one_command_per_target(self):
        self.enable.update(minimum_us=1000,maximum_us=2000)
        self.servo(self.enable);self.active()
        self.s['servo'].update(min_us=1000,max_us=2000)
        for pulse in (1000,1500,2000):
            self.station.session.pending=None
            self.station.ingest(self.s,time.monotonic())
            self.servo(dict(operation='set',channel=1,pulse_us=pulse))
        self.assertEqual(self.station.serial.writes,[b'1 servo enable 1 1000 2000\n',
            b'2 servo set 1 1000\n',b'3 servo set 1 1500\n',b'4 servo set 1 2000\n'])

    def test_parser_exact_arity_and_bounds(self):
        for command in ('servo stop','servo enable 8 900 2100','servo set 1 1520'):
            self.assertTrue(valid_command(command))
        for command in ('servo enable 0 900 2100','servo enable 1 899 2100','servo set 1 2101','servo stop now','servo set 1 nan'):
            self.assertFalse(valid_command(command))

if __name__=='__main__': unittest.main(verbosity=2)
