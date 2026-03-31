# VAPS MON 프로그램

라즈베리파이에서 실행하는 **모니터링 프로그램(MON)** 입니다.

기능:
- 경고 이벤트 기록
- 제동 이벤트 기록
- 제동 시점의 기어/도어/운전자 유무/속도 기록
- 웹 화면에서 최근 기록 조회
- 새 이벤트 수신 시 웹에서 팝업 알림 + 자동 새로고침(SSE)
- UART(시리얼) 수신 또는 HTTP API 수신 지원
- SQLite 기반 로컬 저장

---

## 1) 설치

```bash
sudo apt update
sudo apt install -y python3 python3-pip
cd /home/pi
mkdir -p mon_program
```

이 폴더 안의 파일들을 `/home/pi/mon_program` 로 복사합니다.

패키지 설치:

```bash
cd /home/pi/mon_program
pip3 install -r requirements.txt
```

---

## 2) 실행

```bash
cd /home/pi/mon_program
python3 app.py
```

브라우저에서 접속:

```bash
http://라즈베리파이IP:5000
```

예:

```bash
http://192.168.0.10:5000
```

---

## 3) systemd 서비스 등록

서비스 파일 복사:

```bash
sudo cp /home/pi/mon_program/mon.service /etc/systemd/system/mon.service
sudo systemctl daemon-reload
sudo systemctl enable mon.service
sudo systemctl start mon.service
```

상태 확인:

```bash
sudo systemctl status mon.service
```

로그 확인:

```bash
journalctl -u mon.service -f
```

---

## 4) UART 입력 형식

### 권장 형식 1: JSON 한 줄

```json
{"event_category":"warning","event_type":"1차","event_time":"2026-03-30 14:22:10"}
{"event_category":"brake","event_type":"긴급","event_time":"2026-03-30 14:22:11","gear_state":"D","door_state":"OPEN","driver_present":0,"vehicle_speed":3.2}
```

### 지원 형식 2: key=value

```text
event_category=warning,event_type=강화,event_time=2026-03-30 14:22:10

event_category=brake,event_type=긴급,event_time=2026-03-30 14:22:11,gear_state=D,door_state=OPEN,driver_present=0,vehicle_speed=3.2
```

### 지원 형식 3: pipe 구분 형식

```text
WARN|1차|2026-03-30 14:22:10
BRAKE|긴급|2026-03-30 14:22:11|D|OPEN|0|3.2
```

### 지원 형식 4: MON 이진 프레임/상태워드

엑셀 인터페이스 정의 기준으로 `Status Word = [15:14][13:12][11:10][9][8][7:0]` 를 디캡슐화합니다.

- `warning_type(2bit)` / `brake_type(2bit)` / `gear_state(2bit)` / `door_state(1bit)` / `driver_present(1bit)` / `speed_kmh(8bit)`
- Event Code(`0x01`, `0x11` 등)가 함께 오면 해당 코드 우선으로 이벤트 종류를 결정합니다.

입력 예시:

```text
0110001000110000
000000010110001000110000
AA 01 62 30 53 55
0x11 0x82 0x57
```

---

## 5) HTTP API 테스트

### 이벤트 직접 저장

```bash
curl -X POST http://127.0.0.1:5000/api/events \
  -H "Content-Type: application/json" \
  -d '{
    "event_category":"brake",
    "event_type":"긴급",
    "event_time":"2026-03-30 14:30:00",
    "gear_state":"D",
    "door_state":"OPEN",
    "driver_present":0,
    "vehicle_speed":4.8
  }'
```

### 샘플 데이터 넣기

```bash
curl -X POST http://127.0.0.1:5000/api/test/sample
```

### 최근 이벤트 조회

```bash
curl http://127.0.0.1:5000/api/events?limit=20
```

---

## 6) 환경변수

필요하면 실행 전에 환경변수를 바꿀 수 있습니다.

```bash
export MON_WEB_PORT=5000
export MON_SERIAL_ENABLED=1
export MON_SERIAL_PORT=/dev/ttyUSB0
export MON_SERIAL_BAUDRATE=115200
export MON_MAX_EVENTS=5000
export MON_SERIAL_DUPLICATE_WINDOW_SEC=1.5
export MON_FRAME_SOF_HEX=AA
export MON_FRAME_EOF_HEX=55
export MON_FRAME_REQUIRE_MARKERS=0
export MON_FRAME_REQUIRE_CHECKSUM=0
python3 app.py
```

---

## 7) 요구사항 반영 내용

- **FR-LOG-01**: 경고 발생 시 시각 + 경고 종류 저장
- **FR-LOG-02**: 제동 발생 시 시각 + 제동 종류 저장
- **FR-LOG-03**: 제동 당시 기어 상태 저장
- **FR-LOG-04**: 제동 당시 도어 상태 저장
- **FR-LOG-05**: 제동 당시 운전자 유무 저장
- **FR-LOG-06**: 제동 당시 차량 속도 저장
- 최근 기록을 웹 화면에서 즉시 조회 가능

주의:
- 이벤트 발생 후 **3초 이내 저장** 요구는 소프트웨어 구조상 수신 즉시 DB 저장으로 반영했습니다.
- 실제 만족 여부는 UART 송신 지연, 라즈베리파이 부하, 저장 매체 상태까지 포함해 현장 시험으로 검증해야 합니다.
