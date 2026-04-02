import time
import serial

DEFAULT_PORT = "/dev/serial0"
BAUDRATE = 9600
DEFAULT_VOLUME = 10

# 01/001.mp3 = warning
# 01/002.mp3 = brake
EVENT_MAP = {
    "warning": (1, 1),
    "brake":   (1, 2),
}


def build_frame(cmd: int, param: int = 0, feedback: int = 0) -> bytes:
    version = 0xFF
    length = 0x06
    para_h = (param >> 8) & 0xFF
    para_l = param & 0xFF

    checksum = (-(version + length + cmd + feedback + para_h + para_l)) & 0xFFFF
    chk_h = (checksum >> 8) & 0xFF
    chk_l = checksum & 0xFF

    return bytes([
        0x7E, version, length, cmd, feedback,
        para_h, para_l, chk_h, chk_l, 0xEF
    ])


class PlayerMini:
    def __init__(self, port: str = DEFAULT_PORT, baudrate: int = BAUDRATE, timeout: float = 0.3):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout

    def _send_cmd(self, ser, cmd: int, param: int = 0, feedback: int = 0, delay: float = 0.15):
        frame = build_frame(cmd, param, feedback)
        ser.write(frame)
        ser.flush()
        time.sleep(delay)

    def play_folder_file(self, folder: int, file_no: int, volume: int = DEFAULT_VOLUME):
        if not 0 <= volume <= 30:
            raise ValueError("volume은 0~30 사이여야 합니다.")
        if not 1 <= folder <= 99:
            raise ValueError("folder는 1~99 사이여야 합니다.")
        if not 1 <= file_no <= 255:
            raise ValueError("file_no는 1~255 사이여야 합니다.")

        ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=self.timeout,
        )

        try:
            time.sleep(2.0)  # 모듈 초기화 대기
            self._send_cmd(ser, 0x09, 0x0002, delay=0.25)  # TF 카드 선택
            self._send_cmd(ser, 0x06, volume)              # 볼륨 설정
            param = ((folder & 0xFF) << 8) | (file_no & 0xFF)
            self._send_cmd(ser, 0x0F, param)              # 폴더/파일 재생
        finally:
            ser.close()

    def play_event(self, event_name: str, volume: int = DEFAULT_VOLUME):
        if event_name not in EVENT_MAP:
            raise ValueError(f"지원하지 않는 이벤트입니다: {event_name}")

        folder, file_no = EVENT_MAP[event_name]
        self.play_folder_file(folder, file_no, volume)


# 기본 플레이어 1개 생성
player = PlayerMini()


def play_warning(volume: int = DEFAULT_VOLUME):
    """01/001.mp3 재생"""
    player.play_event("warning", volume)
    time.sleep(1)
    player.play_event("warning", volume)


def play_brake(volume: int = DEFAULT_VOLUME):
    """01/002.mp3 재생"""
    player.play_event("brake", volume)
    time.sleep(1)
    player.play_event("brake", volume)


def play_file(folder: int, file_no: int, volume: int = DEFAULT_VOLUME):
    """원하는 폴더/파일 직접 재생"""
    player.play_folder_file(folder, file_no, volume)

if __name__ == '__main__':
    play_warning(30)