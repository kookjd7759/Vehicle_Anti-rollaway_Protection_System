import RPi.GPIO as GPIO
import time

PIN_R = 22
PIN_G = 27
PIN_B = 17

PWM_FREQ = 1000

# 네 LED가 common anode면 True, common cathode면 False
# 증상:
# - off인데 희미하게 켜짐
# - 색이 반대로 나옴
# 이런 경우 True로 바꿔봐
COMMON_ANODE = False

_initialized = False
_pwm_r = None
_pwm_g = None
_pwm_b = None
warning_color = "off"

# 채널별 밝기 보정
# 보통 G가 너무 밝아서 줄여야 주황이 예쁘게 나옴
GAIN_R = 1.00
GAIN_G = 0.35
GAIN_B = 0.60


def set_warning_color(color):
    global warning_color
    warning_color = str(color or "").strip().lower() or "off"


def get_warning_color():
    return warning_color


def _clamp(value, low=0.0, high=100.0):
    return max(low, min(high, float(value)))


def _apply_gain(value, gain):
    return _clamp(value * gain)


def _to_duty(value):
    duty = _clamp(value)
    if COMMON_ANODE:
        return 100.0 - duty
    return duty


def _init_rgb():
    global _initialized, _pwm_r, _pwm_g, _pwm_b

    if _initialized:
        return

    GPIO.setwarnings(False)
    GPIO.setmode(GPIO.BCM)

    GPIO.setup(PIN_R, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(PIN_G, GPIO.OUT, initial=GPIO.LOW)
    GPIO.setup(PIN_B, GPIO.OUT, initial=GPIO.LOW)

    _pwm_r = GPIO.PWM(PIN_R, PWM_FREQ)
    _pwm_g = GPIO.PWM(PIN_G, PWM_FREQ)
    _pwm_b = GPIO.PWM(PIN_B, PWM_FREQ)

    # 시작은 꺼진 상태
    _pwm_r.start(_to_duty(0))
    _pwm_g.start(_to_duty(0))
    _pwm_b.start(_to_duty(0))

    _initialized = True


def _set_color(r, g, b):
    _init_rgb()

    r = _apply_gain(r, GAIN_R)
    g = _apply_gain(g, GAIN_G)
    b = _apply_gain(b, GAIN_B)

    _pwm_r.ChangeDutyCycle(_to_duty(r))
    _pwm_g.ChangeDutyCycle(_to_duty(g))
    _pwm_b.ChangeDutyCycle(_to_duty(b))


def on_red():
    set_warning_color("red")
    _set_color(100, 0, 0)


def on_orange():
    set_warning_color("orange")
    # 주황은 G를 많이 낮춰야 자연스럽게 나오는 경우가 많음
    _set_color(100, 25, 0)


def on_green():
    set_warning_color("green")
    _set_color(0, 100, 0)


def off():
    set_warning_color("off")
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