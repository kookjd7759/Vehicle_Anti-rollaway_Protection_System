import RPi.GPIO as GPIO
import time

PIN_R = 22
PIN_G = 27
PIN_B = 17

_initialized = False
_pwm_r = None
_pwm_g = None
_pwm_b = None


def _init_rgb():
    global _initialized, _pwm_r, _pwm_g, _pwm_b

    if _initialized:
        return

    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)

    GPIO.setup(PIN_R, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(PIN_G, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(PIN_B, GPIO.OUT, initial=GPIO.LOW)

    _pwm_r = GPIO.PWM(PIN_R, 1000)
    _pwm_g = GPIO.PWM(PIN_G, 1000)
    _pwm_b = GPIO.PWM(PIN_B, 1000)

    _pwm_r.start(0)
    _pwm_g.start(0)
    _pwm_b.start(0)

    _initialized = True


def _set_color(r, g, b):
    _init_rgb()
    _pwm_r.ChangeDutyCycle(r)   # 0~100
    _pwm_g.ChangeDutyCycle(g)
    _pwm_b.ChangeDutyCycle(b)


def on_red():
    _set_color(100, 0, 0)


def on_orange():
    _set_color(100, 20, 0)


def on_green():
    _set_color(0, 100, 0)


def off():
    if not _initialized:
        return
    _set_color(0, 0, 0)


def cleanup():
    global _initialized

    if not _initialized:
        return

    off()
    _pwm_r.stop()
    _pwm_g.stop()
    _pwm_b.stop()
    GPIO.cleanup()
    _initialized = False


def warning_red():
    for _ in range(3):
        on_red()
        time.sleep(0.2)
        off()
        time.sleep(0.1)


def warning_orange():
    for _ in range(3):
        on_orange()
        time.sleep(0.2)
        off()
        time.sleep(0.1)


def warning_green():
    for _ in range(3):
        on_green()
        time.sleep(0.2)
        off()
        time.sleep(0.1)


if __name__ == '__main__':
    try:
        warning_red()
        time.sleep(1)

        warning_orange()
        time.sleep(1)

        warning_green()
        time.sleep(1)

        off()
    finally:
        cleanup()