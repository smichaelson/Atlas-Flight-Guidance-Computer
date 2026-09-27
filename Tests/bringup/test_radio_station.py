"""Radio capability, proof validation and USB command gates using inert serial fixtures."""
import copy
import time
import unittest
from test_servo_station import frames, Serial, gs
from protocol import validate, valid_command, Session

def radio():
    return dict(rx=0, command=0, last_hex='', baud=57600, uart_errors=0, dropped=0,
                monitoring=0, connected=0, waiting=0, peer=[0,0,0], ack_age_ms=0xFFFFFFFF,
                rtt_ms=0, sent=0, received=0, replies=0, acknowledgements=0, timeouts=0,
                tx_errors=0, invalid=0, test=0, test_sequence=0, test_rtt_ms=0, test_peer=[0,0,0])

class RadioTests(unittest.TestCase):
    def setUp(self):
        self.h,self.s=frames(); self.h['radio_link']=1; self.s['radio']=radio()
        self.s['attempted']|=128; self.s['init'][9]=0
        self.station=gs.Station(); self.station.mode='live'; self.station.confirmed=True
        self.station.serial=Serial()
        self.station.ingest(validate(self.h),time.monotonic())
        self.station.ingest(validate(self.s),time.monotonic())

    def test_legacy_telemetry_remains_readable_but_cannot_claim_acknowledged_tests(self):
        h,s=frames(); session=Session(); session.accept(h,1); session.accept(s,1)
        for verb in ('radio ping','radio connect','radio disconnect'):
            with self.assertRaisesRegex(ValueError,'1.4.0'): session.request(verb,1,True)

    def test_allowlist(self):
        for verb in ('radio connect','radio disconnect','radio ping','radio id'):
            self.assertTrue(valid_command(verb))
        for verb in ('radio connect now','radio ping 7','radio send servo on','radio connect\n','radio disconnect '):
            self.assertFalse(valid_command(verb))

    def test_capability_is_versioned_not_truthy(self):
        for value in (True,False,2,'1',0):
            h=copy.deepcopy(self.h);h['radio_link']=value
            with self.assertRaises(ValueError): validate(h)

    def test_new_capability_requires_status(self):
        h,s=frames(); h['radio_link']=1
        session=Session();session.accept(h,1);session.accept(s,1)
        self.assertIn('Radio link telemetry missing',session.blocked)

    def test_all_consumed_link_fields_validated(self):
        for key in radio():
            if key in ('rx','command','last_hex'): continue
            s=copy.deepcopy(self.s);del s['radio'][key]
            with self.assertRaises(ValueError,msg=key): validate(s)
            s=copy.deepcopy(self.s);s['radio'][key]=True
            with self.assertRaises(ValueError,msg=key): validate(s)

    def test_connected_needs_recent_round_trip(self):
        s=copy.deepcopy(self.s);s['radio'].update(connected=1,peer=[1,2,3],ack_age_ms=30,rtt_ms=85,acknowledgements=1)
        validate(s)
        for key,value in (('peer',[0,0,0]),('ack_age_ms',6000),('rtt_ms',2000),('acknowledgements',0),('command',1)):
            wrong=copy.deepcopy(s);wrong['radio'][key]=value
            with self.assertRaises(ValueError,msg=key):validate(wrong)
        for key,value in (('test_peer',[0,0,0]),('test_sequence',0),('test_rtt_ms',2000)):
            wrong=copy.deepcopy(s);wrong['radio'].update(test=2,test_peer=[1,2,3],test_sequence=1,test_rtt_ms=80)
            wrong['radio'][key]=value
            with self.assertRaises(ValueError,msg=key):validate(wrong)

    def test_ack_operations_preserve_stabilization_gates_for_other_commands(self):
        # Session has already accepted valid telemetry; isolate the command policy here.
        for verb in ('radio connect','radio ping','radio disconnect'):
            self.setUp();self.station.session.status['stabilization']={'enabled':1}
            self.station.session.status['gpio']['pwm']=0xC3
            self.station.action('command',dict(verb=verb,action_confirmed=True))
            self.assertEqual(self.station.serial.writes,[f'1 {verb}\n'.encode()])
        for verb in ('radio id','probe radio','beep','spi'):
            self.setUp();self.station.session.status['stabilization']={'enabled':1}
            with self.assertRaises(ValueError):self.station.action('command',dict(verb=verb,action_confirmed=True))
            self.assertEqual(self.station.serial.writes,[])

    def test_pending_stale_demo_and_unconfirmed_never_transmit(self):
        for condition in ('pending','stale','demo','unconfirmed'):
            self.setUp()
            if condition=='pending': self.station.session.request('radio ping',time.monotonic(),True)
            if condition=='stale': self.station.session.received_at=0
            if condition=='demo': self.station.mode='demo'
            with self.assertRaises(ValueError):
                self.station.action('command',dict(verb='radio connect',action_confirmed=condition!='unconfirmed'))
            self.assertEqual(self.station.serial.writes,[])

    def test_timeout_is_explicit_failure_not_host_uncertainty(self):
        self.station.action('command',dict(verb='radio ping',action_confirmed=True))
        self.station.ingest(dict(type='reply',id=1,status=5,verified_bytes=0,name='TIMEOUT',detail='No peer acknowledgement within 2000 ms'),time.monotonic())
        self.assertIsNone(self.station.session.pending)
        self.assertFalse(self.station.session.blocked)
        self.assertEqual(self.station.session.last_reply['status'],5)
        self.assertEqual(self.station.events[-1]['kind'],'error')

if __name__=='__main__':unittest.main()
