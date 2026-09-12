"""Synthetic, explicitly labeled dashboard fixture; never opens a serial port.

Major functions: hello produces demo identity; status produces schema-complete
fake measurements for offline UI/model tests. These are NOT board test results.
"""


def hello() -> dict:
    """@brief Return unmistakable simulation identity. @return Schema-1 hello."""
    return dict(type="hello", profile="bringup", schema=1, version="DEMO-NOT-HARDWARE",
                pwm_pyro_inhibited=True, led_inhibited=True,
                uid=[0, 0, 0], device_id=0, clock_hz=200000000)


def status(ms: int = 10000) -> dict:
    """@brief Return synthetic observations. @param ms Simulated tick. @return Status."""
    return dict(
        type="status", profile="bringup", inhibited=True, schema=1, seq=ms // 500,
        ms=ms, owner_ms=ms, attempted=127, pending_id=0, init=[0] * 9 + [4, 0, 0],
        count=[ms // 5] * 4, errors=[0] * 4, sample_status=[0] * 4, service=0,
        adxl=dict(mg=[0, 0, 1000], t=ms),
        lsm=dict(mg=[0, 0, 1000], mdps=[0, 0, 0], temp_cc=2400, t=ms, irq=ms // 5),
        mmc=dict(nt=[21000, -4000, 39000], t=ms), baro=dict(pa=101325, temp_cc=2350, t=ms),
        bno=dict(count=[ms // 10] * 4, t=[ms] * 4, accuracy=[0] * 4,
                 accel_mm_s2=[0, 0, 9807], gyro_mrad_s=[0, 0, 0], mag_nt=[21000, -4000, 39000],
                 q_ppm=[1000000, 0, 0, 0],
                 health=dict(interrupts=ms // 10, reads=ms // 10, writes=5, io_errors=0,
                             protocol_errors=0, decoded=ms // 10, decode_errors=0, resets=1,
                             recovery_attempts=0, recovery_failures=0, last_hal_status=0,
                             last_hal_error=0, failure_stage=0, last_length=28,
                             pending_length=0, intn_low=0, initialized=1)),
        gnss=dict(version="SIMULATED", t=ms, frames=ms // 100, crc_errors=0, timeouts=0,
                   fix=0, flags=0, sv=0, lat_e7=0, lon_e7=0, h_msl_mm=0,
                   hacc_mm=0, tow_ms=ms, pps_count=0, pps_us=0,
                   failure_stage=0, failure_status=0, pps_started=1,
                   rx_bytes=ms, tx_bytes=8, dropped=0, uart_errors=0, restarts=0,
                   preflights=1, start_retries=0, hal_status=0, hal_error=0),
        power=dict(available=True, start=0, status=0, t=ms, count=ms // 10,
                   valid=1023, vdda_mv=3300, temp_c=25,
                   mv=[3300, 8400, 5000, 15000, 0, 0, 0, 0, 0, 0], raw=[30000] * 10,
                   adc_errors=0, ref_stage=0, ref_channel=1, ref_raw=10000,
                   ref_hal_status=0, ref_hal_error=0,
                   reset_flags=0, power_events=0, ecc_events=0),
        gpio=dict(inputs=0, outputs=0, switch=0, pwm=0, armed=0),
        led=dict(commanded=0, gates=0, initialized=1, inhibited=1),
        sd=dict(start=0, card=0, mounted=0, status=4, fs=3, completed=0, errors=0,
                time_valid=0, utc=[0] * 6),
        ble=dict(model="SIMULATED NINA", firmware="SIMULATED", command=1, dtr=0,
                 rx=0, tx=0, timeouts=0, last_hex=""),
        radio=dict(rx=0, command=0, last_hex=""),
        usb=dict(session=1, rx=0, rx_drop=0, tx=ms, tx_drop=0, timeouts=0),
        tasks=dict(console=ms, owner=ms, watchdog=ms // 100, fault=0, busy=0,
                   parser_errors=0, response_drops=0, stack_words=[1500, 1500, 800, 1000, 700]))
