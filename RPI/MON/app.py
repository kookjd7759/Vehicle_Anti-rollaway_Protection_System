import importlib.util
import json
import os
import queue
import re
import shlex
import socket
import sqlite3
import subprocess
import threading
import time
import webbrowser
from contextlib import closing
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, Optional
from zoneinfo import ZoneInfo

from flask import Flask, Response, jsonify, redirect, render_template, request, stream_with_context, url_for

try:
    import serial  # type: ignore
except Exception:
    serial = None

try:
    from serial.tools import list_ports  # type: ignore
except Exception:
    list_ports = None

try:
    import LED_driver  # type: ignore
except Exception:
    LED_driver = None

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = Path(os.getenv("MON_DB_PATH", BASE_DIR / "mon_logs.db"))
SERIAL_PORT = "/dev/ttyUSB0"
SERIAL_BAUDRATE = int(os.getenv("MON_SERIAL_BAUDRATE", "115200"))
SERIAL_ENABLED = os.getenv("MON_SERIAL_ENABLED", "1") == "1"
SERIAL_RETRY_SEC = float(os.getenv("MON_SERIAL_RETRY_SEC", "3"))
SERIAL_TIMEOUT_SEC = float(os.getenv("MON_SERIAL_TIMEOUT_SEC", "1"))
WEB_HOST = os.getenv("MON_WEB_HOST", "0.0.0.0")
WEB_PORT = int(os.getenv("MON_WEB_PORT", "5000"))
AUTO_REFRESH_SEC = int(os.getenv("MON_AUTO_REFRESH_SEC", "5"))
TIMEZONE_NAME = os.getenv("MON_TIMEZONE", "Asia/Seoul")
SEND_URL_ENABLED = os.getenv("MON_SEND_URL_ENABLED", "1") == "1"
SEND_URL_COMMAND = os.getenv("MON_SEND_URL_COMMAND", "send_url send {url}")
SEND_URL_TIMEOUT_SEC = float(os.getenv("MON_SEND_URL_TIMEOUT_SEC", "20"))
SEND_URL_POLL_INTERVAL_SEC = float(os.getenv("MON_SEND_URL_POLL_INTERVAL_SEC", "0.2"))
PUBLIC_WEB_HOST = os.getenv("MON_PUBLIC_HOST", "").strip()
LED_SIGNAL_ENABLED = os.getenv("MON_LED_SIGNAL_ENABLED", "1") == "1"
MON_BINARY_FRAME_BITS = int(os.getenv("MON_BINARY_FRAME_BITS", "16"))
if MON_BINARY_FRAME_BITS < 8 or MON_BINARY_FRAME_BITS % 8 != 0:
    MON_BINARY_FRAME_BITS = 16

app = Flask(__name__)
app.json.ensure_ascii = False
_db_lock = threading.Lock()
_led_queue: "queue.Queue[str]" = queue.Queue(maxsize=64)
_led_worker_started = False
_led_worker_lock = threading.Lock()
_serial_state_lock = threading.Lock()
_stream_clients_lock = threading.Lock()
_stream_clients: list["queue.Queue[str]"] = []
_stream_ping_sec = 15.0
_serial_state: Dict[str, Any] = {
    "connected": False,
    "retrying": SERIAL_ENABLED,
    "last_error": "",
    "last_error_at": None,
    "last_connected_at": None,
    "last_data_at": None,
    "last_raw": "",
    "reconnect_count": 0,
    "received_count": 0,
    "received_bytes": 0,
    "parsed_count": 0,
    "dropped_count": 0,
}


VALID_CATEGORIES = {"warning", "brake", "system"}
EVENT_WARNING_PRIMARY = "\u0031\ucc28"
EVENT_WARNING_ENHANCED = "\uac15\ud654"
EVENT_BRAKE_D = "D\ub2e8"
EVENT_BRAKE_R = "R\ub2e8"
EVENT_BRAKE_EMERGENCY = "\uae34\uae09"
EVENT_SYSTEM_RELEASE_DRIVER = "\uc81c\ub3d9 \ud574\uc81c(\uc6b4\uc804\uc790 \ubcf5\uadc0)"
EVENT_SYSTEM_RELEASE_P = "\uc81c\ub3d9 \ud574\uc81c(P\ub2e8 \uc804\ud658)"
EVENT_SYSTEM_LOG_SENT = "\uc774\ubca4\ud2b8 \ub85c\uadf8 \uc804\uc1a1 \uc644\ub8cc"
EVENT_SYSTEM_STATUS = "status"

VALID_WARNING_TYPES = {EVENT_WARNING_PRIMARY, EVENT_WARNING_ENHANCED, "Rollaway"}
VALID_BRAKE_TYPES = {EVENT_BRAKE_D, EVENT_BRAKE_R, "Rollaway", EVENT_BRAKE_EMERGENCY}
VALID_GEAR = {"P", "R", "N", "D", "UNKNOWN", ""}
VALID_DOOR = {"OPEN", "CLOSED", "UNKNOWN", ""}

BINARY_WARNING_TYPE_MAP = {
    0: "",
    1: EVENT_WARNING_PRIMARY,
    2: EVENT_WARNING_ENHANCED,
    3: "Rollaway",
}

BINARY_BRAKE_TYPE_MAP = {
    0: "",
    1: EVENT_BRAKE_D,
    2: EVENT_BRAKE_R,
    3: "Rollaway",
}

BINARY_GEAR_STATE_MAP = {
    0: "P",
    1: "R",
    2: "N",
    3: "D",
}


SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_time TEXT NOT NULL,
    event_category TEXT NOT NULL,
    event_type TEXT NOT NULL,
    gear_state TEXT,
    door_state TEXT,
    driver_present INTEGER,
    vehicle_speed REAL,
    source TEXT NOT NULL DEFAULT 'api',
    raw_payload TEXT,
    received_at TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_events_event_time ON events(event_time DESC);
CREATE INDEX IF NOT EXISTS idx_events_category ON events(event_category);
CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);
"""


def get_db() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def init_db() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    with closing(get_db()) as conn:
        conn.executescript(SCHEMA_SQL)
        conn.commit()


def now_str() -> str:
    try:
        return datetime.now(ZoneInfo(TIMEZONE_NAME)).strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _serial_state_update(**kwargs: Any) -> None:
    with _serial_state_lock:
        _serial_state.update(kwargs)


def _serial_state_increment(field: str, amount: int = 1) -> None:
    with _serial_state_lock:
        _serial_state[field] = int(_serial_state.get(field, 0) or 0) + amount


def get_serial_state() -> Dict[str, Any]:
    with _serial_state_lock:
        return dict(_serial_state)


def is_usb_connected(port_name: str) -> bool:
    port = str(port_name or "").strip()
    if not port:
        return False

    # Try pyserial port discovery first.
    if list_ports is not None:
        try:
            normalized = port.lower()
            for info in list_ports.comports():
                if str(getattr(info, "device", "")).strip().lower() == normalized:
                    return True
        except Exception:
            pass

    # For Linux device paths (/dev/ttyUSB0, etc), filesystem existence is a good signal.
    if port.startswith("/dev/"):
        try:
            return Path(port).exists()
        except Exception:
            return False

    return False


def get_connection_flags() -> Dict[str, Any]:
    state = get_serial_state()
    port_open = bool(state.get("connected"))
    usb_connected = is_usb_connected(SERIAL_PORT) or port_open
    connected = bool(SERIAL_ENABLED and usb_connected and port_open)

    if not SERIAL_ENABLED:
        message = "시리얼 비활성화"
    elif connected:
        message = "USB 연결됨 / 포트 열림"
    elif usb_connected:
        message = "USB 연결됨 / 포트 닫힘"
    else:
        message = "USB 연결 안 됨"

    return {
        "connected": connected,
        "usb_connected": usb_connected,
        "port_open": port_open,
        "message": message,
    }


def action_to_warning_color(action: Optional[str]) -> Optional[str]:
    if action == "control":
        return "red"
    if action == "warning":
        return "orange"
    if action in {"release", "system"}:
        return "green"
    return None


def set_led_warning_color(color: Optional[str]) -> None:
    if LED_driver is None or color is None:
        return
    setter = getattr(LED_driver, "set_warning_color", None)
    if callable(setter):
        try:
            setter(color)
            return
        except Exception:
            pass
    try:
        setattr(LED_driver, "warning_color", color)
    except Exception:
        pass


def get_led_warning_color() -> Optional[str]:
    if LED_driver is None:
        return None
    try:
        value = getattr(LED_driver, "warning_color", None)
    except Exception:
        value = None
    if value is None:
        return None
    text = str(value).strip().lower()
    return text or None


def make_sse_event(event_name: str, payload: Dict[str, Any]) -> str:
    data = json.dumps(payload, ensure_ascii=False)
    return f"event: {event_name}\ndata: {data}\n\n"


def notify_stream_clients(event_name: str, payload: Dict[str, Any]) -> None:
    message = make_sse_event(event_name, payload)
    stale_clients: list["queue.Queue[str]"] = []
    with _stream_clients_lock:
        for client_queue in list(_stream_clients):
            try:
                client_queue.put_nowait(message)
            except queue.Full:
                stale_clients.append(client_queue)
        for client_queue in stale_clients:
            if client_queue in _stream_clients:
                _stream_clients.remove(client_queue)


def classify_led_action(cleaned: Dict[str, Any]) -> Optional[str]:
    category = str(cleaned.get("event_category") or "").lower()
    source = str(cleaned.get("source") or "").lower()
    event_type = str(cleaned.get("event_type") or "").lower()

    # Keep sample data from driving hardware signals.
    if source.startswith("sample"):
        return None

    if category == "brake":
        return "control"
    if category == "warning":
        return "warning"
    if category == "system":
        if event_type == EVENT_SYSTEM_STATUS:
            return None
        return "system"
    return None


def _call_led_driver(action: str) -> None:
    if LED_driver is None or not LED_SIGNAL_ENABLED:
        return

    candidate_names: list[str]
    if action == "control":
        candidate_names = ["warning_red", "on_red", "red"]
    elif action == "warning":
        candidate_names = ["warning_orange", "on_orange", "orange"]
    elif action in {"release", "system"}:
        candidate_names = ["warning_green", "on_green", "green", "grean"]
    else:
        return

    for name in candidate_names:
        func = getattr(LED_driver, name, None)
        if callable(func):
            func()
            current_color = get_led_warning_color() or action_to_warning_color(action) or "unknown"
            print(f"[LED] warning_color={current_color} action={action}")
            return

    print(f"[LED] no callable for action={action} candidates={candidate_names}")


def _led_worker() -> None:
    while True:
        action = _led_queue.get()
        try:
            _call_led_driver(action)
        except Exception as exc:
            print(f"[LED] action failed: {action} / {exc}")
        finally:
            _led_queue.task_done()


def _ensure_led_worker_started() -> None:
    global _led_worker_started
    if _led_worker_started:
        return
    with _led_worker_lock:
        if _led_worker_started:
            return
        t = threading.Thread(target=_led_worker, daemon=True)
        t.start()
        _led_worker_started = True


def trigger_led_signal(cleaned: Dict[str, Any]) -> None:
    action = classify_led_action(cleaned)
    if action is None:
        return
    if LED_driver is None or not LED_SIGNAL_ENABLED:
        return

    warning_color = action_to_warning_color(action)
    set_led_warning_color(warning_color)

    _ensure_led_worker_started()
    try:
        _led_queue.put_nowait(action)
    except queue.Full:
        # Keep latest signal by dropping one stale queued action.
        try:
            _ = _led_queue.get_nowait()
            _led_queue.task_done()
        except queue.Empty:
            pass
        try:
            _led_queue.put_nowait(action)
        except queue.Full:
            pass


def build_sample_events(source: str = "sample") -> list[Dict[str, Any]]:
    try:
        base = datetime.now(ZoneInfo(TIMEZONE_NAME))
    except Exception:
        base = datetime.now()

    def ts(minutes_ago: int) -> str:
        return (base - timedelta(minutes=minutes_ago)).strftime("%Y-%m-%d %H:%M:%S")

    return [
        # warning events (FR-LOG-01)
        {"event_category": "warning", "event_type": EVENT_WARNING_PRIMARY, "event_time": ts(42), "source": source},
        {"event_category": "warning", "event_type": EVENT_WARNING_ENHANCED, "event_time": ts(39), "source": source},
        {"event_category": "warning", "event_type": "Rollaway", "event_time": ts(36), "source": source},
        {"event_category": "warning", "event_type": EVENT_WARNING_PRIMARY, "event_time": ts(33), "source": source},
        {"event_category": "warning", "event_type": EVENT_WARNING_ENHANCED, "event_time": ts(30), "source": source},
        # brake events (FR-LOG-02~06)
        {
            "event_category": "brake",
            "event_type": EVENT_BRAKE_D,
            "event_time": ts(27),
            "gear_state": "D",
            "door_state": "OPEN",
            "driver_present": 0,
            "vehicle_speed": 7.9,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": EVENT_BRAKE_D,
            "event_time": ts(24),
            "gear_state": "D",
            "door_state": "CLOSED",
            "driver_present": 0,
            "vehicle_speed": 5.8,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": EVENT_BRAKE_R,
            "event_time": ts(21),
            "gear_state": "R",
            "door_state": "OPEN",
            "driver_present": 0,
            "vehicle_speed": 4.6,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": EVENT_BRAKE_R,
            "event_time": ts(18),
            "gear_state": "R",
            "door_state": "CLOSED",
            "driver_present": 0,
            "vehicle_speed": 3.2,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": "Rollaway",
            "event_time": ts(15),
            "gear_state": "N",
            "door_state": "OPEN",
            "driver_present": 0,
            "vehicle_speed": 2.4,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": EVENT_BRAKE_EMERGENCY,
            "event_time": ts(12),
            "gear_state": "D",
            "door_state": "OPEN",
            "driver_present": 1,
            "vehicle_speed": 9.5,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": EVENT_BRAKE_EMERGENCY,
            "event_time": ts(9),
            "gear_state": "R",
            "door_state": "OPEN",
            "driver_present": 0,
            "vehicle_speed": 8.1,
            "source": source,
        },
        # system events (release/status)
        {
            "event_category": "system",
            "event_type": EVENT_SYSTEM_RELEASE_DRIVER,
            "event_time": ts(6),
            "gear_state": "P",
            "door_state": "CLOSED",
            "driver_present": 1,
            "vehicle_speed": 0,
            "source": source,
        },
        {
            "event_category": "system",
            "event_type": EVENT_SYSTEM_RELEASE_P,
            "event_time": ts(4),
            "gear_state": "P",
            "door_state": "CLOSED",
            "driver_present": 1,
            "vehicle_speed": 0,
            "source": source,
        },
        {
            "event_category": "system",
            "event_type": EVENT_SYSTEM_LOG_SENT,
            "event_time": ts(2),
            "gear_state": "P",
            "door_state": "CLOSED",
            "driver_present": 1,
            "vehicle_speed": 0,
            "source": source,
        },
    ]


def seed_sample_events_if_empty() -> int:
    with closing(get_db()) as conn:
        total = conn.execute("SELECT COUNT(*) FROM events").fetchone()[0]
    if total > 0:
        return 0
    samples = build_sample_events(source="sample_seed")
    for item in samples:
        insert_event(item)
    return len(samples)


def normalize_bool(value: Any) -> Optional[int]:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return 1 if value else 0
    if isinstance(value, (int, float)):
        return 1 if int(value) != 0 else 0
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "y", "driver", "present", "착석", "있음"}:
        return 1
    if text in {"0", "false", "no", "n", "absent", "none", "없음", "부재"}:
        return 0
    return None



def parse_event_time(value: Any) -> str:
    if value is None or str(value).strip() == "":
        return now_str()
    text = str(value).strip()
    candidates = [
        "%Y-%m-%d %H:%M:%S",
        "%Y/%m/%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%dT%H:%M:%S.%f",
    ]
    for fmt in candidates:
        try:
            return datetime.strptime(text, fmt).strftime("%Y-%m-%d %H:%M:%S")
        except ValueError:
            continue
    return text



def normalize_text(value: Any) -> str:
    if value is None:
        return ""
    return str(value).strip()



def normalize_category(value: Any) -> str:
    text = normalize_text(value)
    lower = text.lower()
    mapping = {
        "warn": "warning",
        "warning": "warning",
        "경고": "warning",
        "寃쎄퀬": "warning",
        "brake": "brake",
        "제동": "brake",
        "?쒕룞": "brake",
        "system": "system",
        "sys": "system",
        "시스템": "system",
        "?쒖뒪??": "system",
    }
    normalized = mapping.get(text, mapping.get(lower, lower))
    if normalized in VALID_CATEGORIES:
        return normalized
    if text.startswith("寃쎄퀬"):
        return "warning"
    if text.startswith("?쒕룞"):
        return "brake"
    if text.startswith("?쒖뒪"):
        return "system"
    return lower



def normalize_event_type(category: str, value: Any) -> str:
    text = normalize_text(value)
    lower = text.lower()
    upper = text.upper()

    if category == "warning":
        mapping = {
            "1": EVENT_WARNING_PRIMARY,
            EVENT_WARNING_PRIMARY: EVENT_WARNING_PRIMARY,
            f"{EVENT_WARNING_PRIMARY}경고": EVENT_WARNING_PRIMARY,
            f"{EVENT_WARNING_PRIMARY} 경고": EVENT_WARNING_PRIMARY,
            "1?": EVENT_WARNING_PRIMARY,
            "1??": EVENT_WARNING_PRIMARY,
            "1李?": EVENT_WARNING_PRIMARY,
            "1李④꼍怨?": EVENT_WARNING_PRIMARY,
            "1李?寃쎄퀬": EVENT_WARNING_PRIMARY,

            EVENT_WARNING_ENHANCED: EVENT_WARNING_ENHANCED,
            f"{EVENT_WARNING_ENHANCED}경고": EVENT_WARNING_ENHANCED,
            f"{EVENT_WARNING_ENHANCED} 경고": EVENT_WARNING_ENHANCED,
            "??": EVENT_WARNING_ENHANCED,
            "媛뺥솕": EVENT_WARNING_ENHANCED,
            "媛뺥솕寃쎄퀬": EVENT_WARNING_ENHANCED,
            "媛뺥솕 寃쎄퀬": EVENT_WARNING_ENHANCED,

            "rollaway": "Rollaway",
            "ROLLAWAY": "Rollaway",
        }
        if text in mapping:
            return mapping[text]
        if lower in mapping:
            return mapping[lower]
        if text.startswith("1") and "?" in text:
            return EVENT_WARNING_PRIMARY
        if text in {"?", "??", "???", "????"}:
            return EVENT_WARNING_ENHANCED
        return text

    if category == "brake":
        mapping = {
            "d": EVENT_BRAKE_D,
            "r": EVENT_BRAKE_R,
            EVENT_BRAKE_D: EVENT_BRAKE_D,
            EVENT_BRAKE_R: EVENT_BRAKE_R,

            "D??": EVENT_BRAKE_D,
            "R??": EVENT_BRAKE_R,
            "D?": EVENT_BRAKE_D,
            "R?": EVENT_BRAKE_R,

            "rollaway": "Rollaway",
            "ROLLAWAY": "Rollaway",

            "emergency": EVENT_BRAKE_EMERGENCY,
            EVENT_BRAKE_EMERGENCY: EVENT_BRAKE_EMERGENCY,
            f"{EVENT_BRAKE_EMERGENCY}제동": EVENT_BRAKE_EMERGENCY,
            f"{EVENT_BRAKE_EMERGENCY} 제동": EVENT_BRAKE_EMERGENCY,
            "??": EVENT_BRAKE_EMERGENCY,
            "湲닿툒": EVENT_BRAKE_EMERGENCY,
            "湲닿툒?쒕룞": EVENT_BRAKE_EMERGENCY,
            "湲닿툒 ?쒕룞": EVENT_BRAKE_EMERGENCY,
        }
        if text in mapping:
            return mapping[text]
        if lower in mapping:
            return mapping[lower]
        if upper.startswith("D") and "?" in text:
            return EVENT_BRAKE_D
        if upper.startswith("R") and "?" in text:
            return EVENT_BRAKE_R
        if text in {"?", "??", "???", "????"}:
            return EVENT_BRAKE_EMERGENCY
        return text

    if category == "system":
        mapping = {
            EVENT_SYSTEM_RELEASE_DRIVER: EVENT_SYSTEM_RELEASE_DRIVER,
            EVENT_SYSTEM_RELEASE_P: EVENT_SYSTEM_RELEASE_P,
            EVENT_SYSTEM_LOG_SENT: EVENT_SYSTEM_LOG_SENT,

            "?? ??(??? ??)": EVENT_SYSTEM_RELEASE_DRIVER,
            "?쒕룞 ?댁젣(?댁쟾??蹂듦?)": EVENT_SYSTEM_RELEASE_DRIVER,
            "?? ??(P?? ??)": EVENT_SYSTEM_RELEASE_P,
            "?쒕룞 ?댁젣(P???꾪솚)": EVENT_SYSTEM_RELEASE_P,
            "?? ?? ?? ??": EVENT_SYSTEM_LOG_SENT,
            "?대깽??濡쒓렇 ?꾩넚 ?꾨즺": EVENT_SYSTEM_LOG_SENT,
        }
        if text in mapping:
            return mapping[text]
        if "복귀" in text:
            return EVENT_SYSTEM_RELEASE_DRIVER
        if "전환" in text and "P" in text.upper():
            return EVENT_SYSTEM_RELEASE_P
        if "로그" in text and "완료" in text:
            return EVENT_SYSTEM_LOG_SENT
        if text.startswith("?? ??("):
            if "P" in text.upper():
                return EVENT_SYSTEM_RELEASE_P
            return EVENT_SYSTEM_RELEASE_DRIVER
        return text or "상태"

    return text or "상태"



def normalize_gear(value: Any) -> str:
    text = normalize_text(value).upper()
    if text == "DRIVE":
        return "D"
    if text == "REVERSE":
        return "R"
    if text == "NEUTRAL":
        return "N"
    if text == "PARK":
        return "P"
    if text in VALID_GEAR:
        return text
    return text or ""



def normalize_door(value: Any) -> str:
    text = normalize_text(value).upper()
    mapping = {
        "OPENED": "OPEN",
        "OPEN": "OPEN",
        "열림": "OPEN",
        "CLOSE": "CLOSED",
        "CLOSED": "CLOSED",
        "닫힘": "CLOSED",
    }
    return mapping.get(text, text if text else "")



def normalize_speed(value: Any) -> Optional[float]:
    if value is None or value == "":
        return None
    try:
        return round(float(value), 2)
    except (TypeError, ValueError):
        return None



def validate_payload(payload: Dict[str, Any]) -> Dict[str, Any]:
    category = normalize_category(payload.get("event_category"))
    if category not in VALID_CATEGORIES:
        raise ValueError("event_category는 warning, brake, system 중 하나여야 합니다.")

    event_type = normalize_event_type(category, payload.get("event_type"))
    if category == "warning" and event_type not in VALID_WARNING_TYPES:
        raise ValueError("warning 이벤트 종류는 1차, 강화, Rollaway 중 하나여야 합니다.")
    if category == "brake" and event_type not in VALID_BRAKE_TYPES:
        raise ValueError("brake 이벤트 종류는 D단, R단, Rollaway, 긴급 중 하나여야 합니다.")
    if not event_type:
        raise ValueError("event_type이 필요합니다.")

    gear_state = normalize_gear(payload.get("gear_state"))
    if gear_state and gear_state not in VALID_GEAR:
        raise ValueError("gear_state는 P, R, N, D, UNKNOWN 중 하나여야 합니다.")

    door_state = normalize_door(payload.get("door_state"))
    if door_state and door_state not in VALID_DOOR:
        raise ValueError("door_state는 OPEN, CLOSED, UNKNOWN 중 하나여야 합니다.")

    cleaned = {
        "event_time": parse_event_time(payload.get("event_time")),
        "event_category": category,
        "event_type": event_type,
        "gear_state": gear_state or None,
        "door_state": door_state or None,
        "driver_present": normalize_bool(payload.get("driver_present")),
        "vehicle_speed": normalize_speed(payload.get("vehicle_speed")),
        "source": normalize_text(payload.get("source")) or "api",
        "raw_payload": json.dumps(payload, ensure_ascii=False),
        "received_at": now_str(),
    }
    return cleaned



def insert_event(payload: Dict[str, Any]) -> int:
    cleaned = validate_payload(payload)
    sql = """
    INSERT INTO events (
        event_time, event_category, event_type, gear_state, door_state,
        driver_present, vehicle_speed, source, raw_payload, received_at
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """
    with _db_lock:
        with closing(get_db()) as conn:
            cur = conn.execute(
                sql,
                (
                    cleaned["event_time"],
                    cleaned["event_category"],
                    cleaned["event_type"],
                    cleaned["gear_state"],
                    cleaned["door_state"],
                    cleaned["driver_present"],
                    cleaned["vehicle_speed"],
                    cleaned["source"],
                    cleaned["raw_payload"],
                    cleaned["received_at"],
                ),
            )
            conn.commit()
            event_id = int(cur.lastrowid)

    # LED 호출은 DB 잠금 밖에서 비동기로 처리
    trigger_led_signal(cleaned)

    action = classify_led_action(cleaned)
    led_warning_color = action_to_warning_color(action) or get_led_warning_color()
    stream_payload: Dict[str, Any] = {
        "id": event_id,
        "event_time": cleaned.get("event_time"),
        "event_category": cleaned.get("event_category"),
        "event_type": cleaned.get("event_type"),
        "source": cleaned.get("source"),
        "led_warning_color": led_warning_color,
        "received_at": cleaned.get("received_at"),
    }
    notify_stream_clients("new_event", stream_payload)
    return event_id



def normalize_existing_event_rows() -> int:
    with _db_lock:
        with closing(get_db()) as conn:
            rows = conn.execute("SELECT id, event_category, event_type FROM events").fetchall()
            updates: list[tuple[str, str, int]] = []
            for row in rows:
                category = normalize_category(row["event_category"])
                event_type = normalize_event_type(category, row["event_type"])
                if category != row["event_category"] or event_type != row["event_type"]:
                    updates.append((category, event_type, int(row["id"])))

            if not updates:
                return 0

            conn.executemany(
                "UPDATE events SET event_category = ?, event_type = ? WHERE id = ?",
                updates,
            )
            conn.commit()
            return len(updates)


def query_events(limit: int = 50, category: str = "", keyword: str = ""):
    limit = max(1, min(limit, 500))
    sql = "SELECT * FROM events WHERE 1=1"
    params = []
    if category:
        sql += " AND event_category = ?"
        params.append(category)
    if keyword:
        sql += " AND (event_type LIKE ? OR gear_state LIKE ? OR door_state LIKE ? OR raw_payload LIKE ?)"
        like = f"%{keyword}%"
        params.extend([like, like, like, like])
    sql += " ORDER BY event_time DESC, id DESC LIMIT ?"
    params.append(limit)

    with closing(get_db()) as conn:
        rows = conn.execute(sql, params).fetchall()
        items: list[Dict[str, Any]] = []
        for row in rows:
            item = dict(row)
            item["event_category"] = normalize_category(item.get("event_category"))
            item["event_type"] = normalize_event_type(item["event_category"], item.get("event_type"))
            items.append(item)
        return items



def get_stats() -> Dict[str, Any]:
    with closing(get_db()) as conn:
        total = conn.execute("SELECT COUNT(*) FROM events").fetchone()[0]
        warning_count = conn.execute("SELECT COUNT(*) FROM events WHERE event_category='warning'").fetchone()[0]
        brake_count = conn.execute("SELECT COUNT(*) FROM events WHERE event_category='brake'").fetchone()[0]
        latest = conn.execute(
            "SELECT event_time, event_category, event_type FROM events ORDER BY event_time DESC, id DESC LIMIT 1"
        ).fetchone()

    latest_item = dict(latest) if latest else None
    if latest_item:
        latest_item["event_category"] = normalize_category(latest_item.get("event_category"))
        latest_item["event_type"] = normalize_event_type(
            latest_item["event_category"], latest_item.get("event_type")
        )

    return {
        "total": total,
        "warning_count": warning_count,
        "brake_count": brake_count,
        "latest": latest_item,
    }



def parse_pipe_message(line: str) -> Optional[Dict[str, Any]]:
    # WARN|1차|2026-03-30 14:22:10
    # BRAKE|긴급|2026-03-30 14:22:11|D|OPEN|0|3.2
    parts = [p.strip() for p in line.split("|")]
    if not parts:
        return None
    head = parts[0].upper()
    if head == "WARN" and len(parts) >= 3:
        return {
            "event_category": "warning",
            "event_type": parts[1],
            "event_time": parts[2],
            "source": "serial",
        }
    if head == "BRAKE" and len(parts) >= 7:
        return {
            "event_category": "brake",
            "event_type": parts[1],
            "event_time": parts[2],
            "gear_state": parts[3],
            "door_state": parts[4],
            "driver_present": parts[5],
            "vehicle_speed": parts[6],
            "source": "serial",
        }
    return None



def parse_kv_message(line: str) -> Optional[Dict[str, Any]]:
    if "=" not in line:
        return None
    result: Dict[str, Any] = {}
    for piece in line.split(","):
        if "=" not in piece:
            continue
        key, value = piece.split("=", 1)
        result[key.strip()] = value.strip()
    if result:
        result.setdefault("source", "serial")
        return result
    return None


def is_binary_line(line: str) -> bool:
    compact = re.sub(r"\s+", "", line)
    return bool(compact) and bool(re.fullmatch(r"[01]+", compact))


def split_binary_frames(bit_stream: str) -> tuple[list[str], str]:
    if not bit_stream:
        return [], ""
    usable_len = len(bit_stream) - (len(bit_stream) % MON_BINARY_FRAME_BITS)
    if usable_len <= 0:
        return [], bit_stream
    frames = [
        bit_stream[i:i + MON_BINARY_FRAME_BITS]
        for i in range(0, usable_len, MON_BINARY_FRAME_BITS)
    ]
    return frames, bit_stream[usable_len:]


def decode_binary_frame(frame_bits: str) -> Dict[str, Any]:
    # Matches CLU/UART_driver/uart_msg.c:sendData()
    # [15:14] warning_type, [13:12] brake_type, [11:10] gear_state,
    # [9] door_state, [8] driver_present, [7:0] speed_kmh
    if len(frame_bits) != MON_BINARY_FRAME_BITS:
        raise ValueError(f"binary frame length must be {MON_BINARY_FRAME_BITS} bits")

    byte_values = [
        int(frame_bits[i:i + 8], 2)
        for i in range(0, len(frame_bits), 8)
    ]
    status_word = int(frame_bits, 2)

    warning_code = (status_word >> 14) & 0x03
    brake_code = (status_word >> 12) & 0x03
    gear_code = (status_word >> 10) & 0x03
    door_code = (status_word >> 9) & 0x01
    driver_code = (status_word >> 8) & 0x01
    speed_code = status_word & 0xFF

    warning_type = BINARY_WARNING_TYPE_MAP.get(warning_code, "")
    brake_type = BINARY_BRAKE_TYPE_MAP.get(brake_code, "")

    if brake_type:
        event_category = "brake"
        event_type = brake_type
    elif warning_type:
        event_category = "warning"
        event_type = warning_type
    else:
        event_category = "system"
        event_type = EVENT_SYSTEM_STATUS

    gear_state = BINARY_GEAR_STATE_MAP.get(gear_code, "UNKNOWN")
    door_state = "OPEN" if door_code else "CLOSED"
    driver_present = 1 if driver_code else 0
    vehicle_speed = float(speed_code)

    payload: Dict[str, Any] = {
        "event_time": now_str(),
        "event_category": event_category,
        "event_type": event_type,
        "warning_type_code": warning_code,
        "warning_type": warning_type or None,
        "brake_type_code": brake_code,
        "brake_type": brake_type or None,
        "gear_state": gear_state,
        "door_state": door_state,
        "driver_present": driver_present,
        "vehicle_speed": vehicle_speed,
        "source": "serial_binary",
        "status_word": f"0x{status_word:04X}",
        "binary_bits": frame_bits,
        "binary_bytes_hex": [f"0x{b:02X}" for b in byte_values],
    }
    return payload


def parse_binary_payloads(line: str, residual_bits: str = "") -> tuple[list[Dict[str, Any]], str]:
    compact = re.sub(r"\s+", "", line)
    if not compact:
        return [], residual_bits
    stream = residual_bits + compact
    frames, remain = split_binary_frames(stream)
    payloads = [decode_binary_frame(frame_bits) for frame_bits in frames]
    return payloads, remain



def parse_serial_line(line: str) -> Optional[Dict[str, Any]]:
    line = line.strip()
    if not line:
        return None

    try:
        payload = json.loads(line)
        if isinstance(payload, dict):
            payload.setdefault("source", "serial")
            return payload
    except json.JSONDecodeError:
        pass

    parsed = parse_kv_message(line)
    if parsed:
        return parsed

    parsed = parse_pipe_message(line)
    if parsed:
        return parsed

    return None



def serial_worker() -> None:
    if not SERIAL_ENABLED:
        print("[SERIAL] 비활성화 상태입니다.")
        return
    if serial is None:
        print("[SERIAL] pyserial이 설치되지 않아 시리얼 수신을 시작하지 않습니다.")
        return

    while True:
        try:
            print(f"[SERIAL] 포트 연결 시도: {SERIAL_PORT} @ {SERIAL_BAUDRATE}")
            with serial.Serial(SERIAL_PORT, SERIAL_BAUDRATE, timeout=1) as ser:
                print("[SERIAL] 연결 성공")
                while True:
                    raw = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not raw:
                        continue
                    payload = parse_serial_line(raw)
                    if not payload:
                        print(f"[SERIAL] 파싱 실패: {raw}")
                        continue
                    try:
                        event_id = insert_event(payload)
                        print(f"[SERIAL] 저장 완료 id={event_id} payload={payload}")
                    except Exception as exc:
                        print(f"[SERIAL] 저장 실패: {exc} / raw={raw}")
        except Exception as exc:
            print(f"[SERIAL] 연결 오류: {exc}")
            time.sleep(3)


def serial_worker_reliable() -> None:
    if not SERIAL_ENABLED:
        print("[SERIAL] disabled")
        _serial_state_update(connected=False, retrying=False, last_error="serial disabled")
        return
    if serial is None:
        print("[SERIAL] pyserial unavailable")
        _serial_state_update(connected=False, retrying=False, last_error="pyserial unavailable")
        return

    while True:
        try:
            _serial_state_update(retrying=True, connected=False)
            print(f"[SERIAL] connect try: {SERIAL_PORT} @ {SERIAL_BAUDRATE}")

            serial_kwargs: Dict[str, Any] = {
                "port": SERIAL_PORT,
                "baudrate": SERIAL_BAUDRATE,
                "timeout": SERIAL_TIMEOUT_SEC,
                "xonxoff": False,
                "rtscts": False,
                "dsrdtr": False,
            }
            if hasattr(serial, "EIGHTBITS"):
                serial_kwargs["bytesize"] = serial.EIGHTBITS
            if hasattr(serial, "PARITY_NONE"):
                serial_kwargs["parity"] = serial.PARITY_NONE
            if hasattr(serial, "STOPBITS_ONE"):
                serial_kwargs["stopbits"] = serial.STOPBITS_ONE

            try:
                ser_obj = serial.Serial(exclusive=False, **serial_kwargs)
            except TypeError:
                ser_obj = serial.Serial(**serial_kwargs)

            with ser_obj as ser:
                try:
                    ser.reset_input_buffer()
                except Exception:
                    pass

                _serial_state_update(
                    connected=True,
                    retrying=False,
                    last_error="",
                    last_connected_at=now_str(),
                )
                print("[SERIAL] connected")
                binary_residual = ""
                text_residual = ""

                def process_serial_record(raw_record: str) -> None:
                    nonlocal binary_residual
                    raw = raw_record.strip()
                    if not raw:
                        return

                    payloads: list[Dict[str, Any]] = []
                    if is_binary_line(raw):
                        payloads, binary_residual = parse_binary_payloads(raw, binary_residual)
                        if not payloads:
                            return
                    else:
                        binary_residual = ""
                        payload = parse_serial_line(raw)
                        if payload:
                            payloads = [payload]

                    if not payloads:
                        _serial_state_increment("dropped_count", 1)
                        print(f"[SERIAL] parse failed: {raw}")
                        return

                    for payload in payloads:
                        try:
                            event_id = insert_event(payload)
                            _serial_state_increment("parsed_count", 1)
                            print(f"[SERIAL] saved id={event_id} payload={payload}")
                        except Exception as exc:
                            _serial_state_increment("dropped_count", 1)
                            _serial_state_update(last_error=str(exc), last_error_at=now_str())
                            print(f"[SERIAL] save failed: {exc} / raw={raw}")

                while True:
                    raw_bytes = ser.read(256)
                    if not raw_bytes:
                        continue

                    decoded = raw_bytes.decode("utf-8", errors="ignore")
                    if not decoded:
                        continue

                    _serial_state_increment("received_count", 1)
                    _serial_state_increment("received_bytes", len(raw_bytes))
                    _serial_state_update(last_data_at=now_str(), last_raw=decoded.strip()[:200])

                    text_residual += decoded
                    lines = re.split(r"[\r\n]+", text_residual)
                    text_residual = lines.pop() if lines else ""

                    for line in lines:
                        process_serial_record(line)

                    if text_residual and is_binary_line(text_residual):
                        process_serial_record(text_residual)
                        text_residual = ""
                    elif len(text_residual) > 4096:
                        _serial_state_increment("dropped_count", 1)
                        print("[SERIAL] residual overflow, dropped")
                        text_residual = ""
                        binary_residual = ""
        except Exception as exc:
            _serial_state_increment("reconnect_count", 1)
            _serial_state_update(
                connected=False,
                retrying=True,
                last_error=str(exc),
                last_error_at=now_str(),
            )
            print(f"[SERIAL] connect error: {exc}")
            time.sleep(max(0.5, SERIAL_RETRY_SEC))


def serial_worker() -> None:
    # Backward-compatible alias
    serial_worker_reliable()


def resolve_public_web_host() -> str:
    if PUBLIC_WEB_HOST:
        return PUBLIC_WEB_HOST
    if WEB_HOST in {"0.0.0.0", "::", ""}:
        return "127.0.0.1"
    return WEB_HOST


def build_send_url_command(app_url: str) -> list[str]:
    template = SEND_URL_COMMAND.strip()
    if not template:
        return []
    tokens = shlex.split(template)
    return [token.format(url=app_url) for token in tokens]


def open_web_url(app_url: str) -> bool:
    try:
        opened = webbrowser.open(app_url, new=0, autoraise=False)
        if opened:
            print(f"[WEB] browser open requested: {app_url}")
        else:
            print(f"[WEB] browser handler unavailable: {app_url}")
        return bool(opened)
    except Exception as exc:
        print(f"[WEB] browser open error: {exc}")
        return False


def run_send_url_module_send() -> bool:
    candidate_paths = [
        BASE_DIR / "send_url.py",
        BASE_DIR.parent / "send_url.py",
    ]

    for path in candidate_paths:
        if not path.exists():
            continue
        try:
            spec = importlib.util.spec_from_file_location("send_url_runtime", path)
            if spec is None or spec.loader is None:
                continue

            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            send_func = getattr(module, "send", None)
            if not callable(send_func):
                print(f"[SEND_URL] send() not found in {path}")
                continue

            send_func()
            print(f"[SEND_URL] executed send_url.send() from {path}")
            return True
        except Exception as exc:
            print(f"[SEND_URL] module call failed for {path}: {exc}")

    return False


def send_url_worker() -> None:
    if not SEND_URL_ENABLED:
        print("[SEND_URL] disabled")
        return

    probe_host = "127.0.0.1" if WEB_HOST in {"0.0.0.0", "::", ""} else WEB_HOST
    deadline = time.time() + SEND_URL_TIMEOUT_SEC

    while time.time() < deadline:
        try:
            with socket.create_connection((probe_host, WEB_PORT), timeout=1):
                break
        except OSError:
            time.sleep(SEND_URL_POLL_INTERVAL_SEC)
    else:
        print("[SEND_URL] web server was not ready in time")
        return

    app_url = f"http://{resolve_public_web_host()}:{WEB_PORT}"
    has_gui = bool(os.getenv("DISPLAY") or os.getenv("WAYLAND_DISPLAY"))
    if has_gui:
        if not open_web_url(app_url):
            print("[WEB] browser open failed, continue send in service mode.")
    else:
        print("[WEB] headless mode detected, skip browser open.")

    if run_send_url_module_send():
        return

    command = build_send_url_command(app_url)
    if not command:
        print("[SEND_URL] MON_SEND_URL_COMMAND is empty")
        return

    try:
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        if completed.returncode == 0:
            print(f"[SEND_URL] executed for {app_url}")
            if completed.stdout.strip():
                print(f"[SEND_URL][stdout] {completed.stdout.strip()}")
        else:
            print(f"[SEND_URL] failed (code={completed.returncode})")
            if completed.stderr.strip():
                print(f"[SEND_URL][stderr] {completed.stderr.strip()}")
    except FileNotFoundError:
        print(f"[SEND_URL] command not found: {command[0]}")
        print(f"[SEND_URL] app url: {app_url}")
        print("[SEND_URL] hint: set MON_SEND_URL_COMMAND to your real sender command.")
    except Exception as exc:
        print(f"[SEND_URL] error: {exc}")


@app.route("/")
def index():
    category = normalize_category(request.args.get("category", ""))
    if category not in VALID_CATEGORIES:
        category = ""
    keyword = request.args.get("keyword", "").strip()
    try:
        limit = int(request.args.get("limit", "50"))
    except ValueError:
        limit = 50

    stats = get_stats()
    events = query_events(limit=limit, category=category, keyword=keyword)
    return render_template(
        "index.html",
        stats=stats,
        events=events,
        selected_category=category,
        keyword=keyword,
        limit=limit,
        auto_refresh_sec=AUTO_REFRESH_SEC,
    )


@app.route("/health")
def health():
    serial_state = get_serial_state()
    connection = get_connection_flags()
    return jsonify({
        "status": "ok",
        "connected": bool(connection.get("connected")),
        "usb_connected": bool(connection.get("usb_connected")),
        "port_open": bool(connection.get("port_open")),
        "message": connection.get("message"),
        "db_path": str(DB_PATH),
        "serial_enabled": SERIAL_ENABLED,
        "serial_port": SERIAL_PORT,
        "serial_baudrate": SERIAL_BAUDRATE,
        "serial_connected": bool(serial_state.get("connected")),
        "serial_retrying": bool(serial_state.get("retrying")),
        "serial_status": connection.get("message"),
        "serial_last_error": serial_state.get("last_error"),
        "serial_last_error_at": serial_state.get("last_error_at"),
        "serial_last_connected_at": serial_state.get("last_connected_at"),
        "serial_last_data_at": serial_state.get("last_data_at"),
        "serial_last_raw": serial_state.get("last_raw"),
        "serial_reconnect_count": serial_state.get("reconnect_count"),
        "serial_received_count": serial_state.get("received_count"),
        "serial_received_bytes": serial_state.get("received_bytes"),
        "serial_parsed_count": serial_state.get("parsed_count"),
        "serial_dropped_count": serial_state.get("dropped_count"),
        "led_signal_enabled": LED_SIGNAL_ENABLED,
        "led_driver_available": LED_driver is not None,
        "send_url_enabled": SEND_URL_ENABLED,
        "now": now_str(),
    })


@app.route("/api/serial/status")
def api_serial_status():
    state = get_serial_state()
    connection = get_connection_flags()
    return jsonify({
        "ok": True,
        "enabled": SERIAL_ENABLED,
        "port": SERIAL_PORT,
        "baudrate": SERIAL_BAUDRATE,
        "connected": bool(connection.get("connected")),
        "usb_connected": bool(connection.get("usb_connected")),
        "port_open": bool(connection.get("port_open")),
        "retrying": bool(state.get("retrying")),
        "message": connection.get("message"),
        "last_error": state.get("last_error"),
        "last_error_at": state.get("last_error_at"),
        "last_connected_at": state.get("last_connected_at"),
        "last_data_at": state.get("last_data_at"),
        "last_raw": state.get("last_raw"),
        "reconnect_count": state.get("reconnect_count"),
        "received_count": state.get("received_count"),
        "received_bytes": state.get("received_bytes"),
        "parsed_count": state.get("parsed_count"),
        "dropped_count": state.get("dropped_count"),
        "now": now_str(),
    })


@app.route("/api/stream")
def api_stream():
    client_queue: "queue.Queue[str]" = queue.Queue(maxsize=64)
    with _stream_clients_lock:
        _stream_clients.append(client_queue)

    @stream_with_context
    def event_stream():
        hello_payload = {
            "ok": True,
            "time": now_str(),
            **get_connection_flags(),
        }
        yield make_sse_event("hello", hello_payload)

        last_ping = time.monotonic()
        try:
            while True:
                wait_sec = max(0.2, _stream_ping_sec - (time.monotonic() - last_ping))
                try:
                    message = client_queue.get(timeout=wait_sec)
                    yield message
                    last_ping = time.monotonic()
                except queue.Empty:
                    yield make_sse_event("ping", {"time": now_str()})
                    last_ping = time.monotonic()
        finally:
            with _stream_clients_lock:
                if client_queue in _stream_clients:
                    _stream_clients.remove(client_queue)

    headers = {
        "Cache-Control": "no-cache, no-transform",
        "Connection": "keep-alive",
        "X-Accel-Buffering": "no",
    }
    return Response(event_stream(), mimetype="text/event-stream", headers=headers)


@app.route("/api/events", methods=["GET"])
def api_get_events():
    category = normalize_category(request.args.get("category", ""))
    if category not in VALID_CATEGORIES:
        category = ""
    keyword = request.args.get("keyword", "").strip()
    try:
        limit = int(request.args.get("limit", "50"))
    except ValueError:
        limit = 50

    return jsonify({
        "items": query_events(limit=limit, category=category, keyword=keyword),
        "stats": get_stats(),
    })


@app.route("/api/events", methods=["POST"])
def api_create_event():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify({"ok": False, "message": "JSON 객체를 보내야 합니다."}), 400
    try:
        event_id = insert_event(payload)
        return jsonify({"ok": True, "id": event_id})
    except Exception as exc:
        return jsonify({"ok": False, "message": str(exc)}), 400


@app.route("/api/test/sample", methods=["POST"])
def api_insert_sample():
    samples = build_sample_events(source="sample_api")
    ids = [insert_event(item) for item in samples]
    return jsonify({"ok": True, "ids": ids})


@app.route("/sample")
def sample_redirect():
    return redirect(url_for("api_insert_sample"))


@app.template_filter("yesno")
def yesno_filter(value: Any) -> str:
    if value is None:
        return "-"
    return "있음" if int(value) == 1 else "없음"


@app.template_filter("category_ko")
def category_ko_filter(value: str) -> str:
    return {
        "warning": "경고",
        "brake": "제동",
        "system": "시스템",
    }.get(value, value)


@app.template_filter("speed_fmt")
def speed_fmt_filter(value: Any) -> str:
    if value is None:
        return "-"
    try:
        return f"{float(value):.2f} km/h"
    except Exception:
        return str(value)



def start_background_threads() -> None:
    if SERIAL_ENABLED:
        t = threading.Thread(target=serial_worker_reliable, daemon=True)
        t.start()
    if SEND_URL_ENABLED:
        t2 = threading.Thread(target=send_url_worker, daemon=True)
        t2.start()


if __name__ == "__main__":
    init_db()
    seeded_count = seed_sample_events_if_empty()
    if seeded_count:
        print(f"[SEED] inserted {seeded_count} sample events")
    normalized_count = normalize_existing_event_rows()
    if normalized_count:
        print(f"[FIX] normalized {normalized_count} event rows")
    start_background_threads()
    print(f"[WEB] http://{WEB_HOST}:{WEB_PORT}")
    app.run(host=WEB_HOST, port=WEB_PORT, debug=False)
