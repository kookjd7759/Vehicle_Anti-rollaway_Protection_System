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


def _hex_env(name: str, default: int) -> int:
    text = os.getenv(name, f"{default:02X}").strip()
    try:
        value = int(text, 16)
    except ValueError:
        return default
    if 0 <= value <= 0xFF:
        return value
    return default


BASE_DIR = Path(__file__).resolve().parent
DB_PATH = Path(os.getenv("MON_DB_PATH", BASE_DIR / "mon_logs.db"))
SERIAL_PORT = os.getenv("MON_SERIAL_PORT", "/dev/ttyUSB0")
SERIAL_BAUDRATE = int(os.getenv("MON_SERIAL_BAUDRATE", "115200"))
SERIAL_ENABLED = os.getenv("MON_SERIAL_ENABLED", "1") == "1"
WEB_HOST = os.getenv("MON_WEB_HOST", "0.0.0.0")
WEB_PORT = int(os.getenv("MON_WEB_PORT", "5000"))
AUTO_REFRESH_SEC = int(os.getenv("MON_AUTO_REFRESH_SEC", "0"))
TIMEZONE_NAME = os.getenv("MON_TIMEZONE", "Asia/Seoul")
SEND_URL_ENABLED = os.getenv("MON_SEND_URL_ENABLED", "1") == "1"
SEND_URL_COMMAND = os.getenv("MON_SEND_URL_COMMAND", "send_url send {url}")
SEND_URL_TIMEOUT_SEC = float(os.getenv("MON_SEND_URL_TIMEOUT_SEC", "20"))
SEND_URL_POLL_INTERVAL_SEC = float(os.getenv("MON_SEND_URL_POLL_INTERVAL_SEC", "0.2"))
PUBLIC_WEB_HOST = os.getenv("MON_PUBLIC_HOST", "").strip()
SERIAL_DUPLICATE_WINDOW_SEC = float(os.getenv("MON_SERIAL_DUPLICATE_WINDOW_SEC", "1.5"))
MAX_EVENTS = int(os.getenv("MON_MAX_EVENTS", "5000"))
EVENT_STREAM_HEARTBEAT_SEC = float(os.getenv("MON_EVENT_STREAM_HEARTBEAT_SEC", "15"))
FRAME_SOF = _hex_env("MON_FRAME_SOF_HEX", 0xAA)
FRAME_EOF = _hex_env("MON_FRAME_EOF_HEX", 0x55)
FRAME_REQUIRE_MARKERS = os.getenv("MON_FRAME_REQUIRE_MARKERS", "0") == "1"
FRAME_REQUIRE_CHECKSUM = os.getenv("MON_FRAME_REQUIRE_CHECKSUM", "0") == "1"
SERIAL_PORT_FALLBACKS = tuple(
    p.strip()
    for p in os.getenv("MON_SERIAL_PORT_FALLBACKS", "/dev/ttyUSB0,/dev/serial0,/dev/ttyAMA0").split(",")
    if p.strip()
)


app = Flask(__name__)
app.json.ensure_ascii = False
_db_lock = threading.Lock()
_subscriber_lock = threading.Lock()
_subscriber_queues: set[queue.Queue[str]] = set()
_serial_state_lock = threading.Lock()
_last_serial_signature = ""
_last_serial_saved_at = 0.0


VALID_CATEGORIES = {"warning", "brake", "system"}
EVENT_WARNING_PRIMARY = "\u0031\ucc28"
EVENT_WARNING_ENHANCED = "\uac15\ud654"
EVENT_BRAKE_D = "D\ub2e8"
EVENT_BRAKE_R = "R\ub2e8"
EVENT_BRAKE_EMERGENCY = "\uae34\uae09"
EVENT_SYSTEM_RELEASE_DRIVER = "\uc81c\ub3d9 \ud574\uc81c(\uc6b4\uc804\uc790 \ubcf5\uadc0)"
EVENT_SYSTEM_RELEASE_P = "\uc81c\ub3d9 \ud574\uc81c(P\ub2e8 \uc804\ud658)"
EVENT_SYSTEM_RELEASE_MANUAL = "\uc81c\ub3d9 \ud574\uc81c(\uc218\ub3d9 \ud574\uc81c)"
EVENT_SYSTEM_RELEASE_RESET = "\uc81c\ub3d9 \ud574\uc81c(\uc2dc\uc2a4\ud15c \ub9ac\uc14b/\uc624\ub958 \ud574\uc81c)"
EVENT_SYSTEM_STATUS_PERIODIC = "\uc8fc\uae30 \uc0c1\ud0dc \uc804\uc1a1"
EVENT_SYSTEM_LOG_SENT = "\uc774\ubca4\ud2b8 \ub85c\uadf8 \uc804\uc1a1 \uc644\ub8cc"

VALID_WARNING_TYPES = {EVENT_WARNING_PRIMARY, EVENT_WARNING_ENHANCED, "Rollaway"}
VALID_BRAKE_TYPES = {EVENT_BRAKE_D, EVENT_BRAKE_R, "Rollaway", EVENT_BRAKE_EMERGENCY}
VALID_GEAR = {"P", "R", "N", "D", "UNKNOWN", ""}
VALID_DOOR = {"OPEN", "CLOSED", "UNKNOWN", ""}

WARNING_TYPE_FROM_BITS = {
    0: None,
    1: EVENT_WARNING_PRIMARY,
    2: EVENT_WARNING_ENHANCED,
    3: "Rollaway",
}
BRAKE_TYPE_FROM_BITS = {
    0: None,
    1: EVENT_BRAKE_D,
    2: EVENT_BRAKE_R,
    3: "Rollaway",
}
GEAR_FROM_BITS = {
    0: "P",
    1: "R",
    2: "N",
    3: "D",
}
DOOR_FROM_BIT = {
    0: "CLOSED",
    1: "OPEN",
}
EVENT_CODE_MAP: dict[int, tuple[str, str]] = {
    0x01: ("warning", EVENT_WARNING_PRIMARY),
    0x02: ("warning", EVENT_WARNING_ENHANCED),
    0x03: ("warning", "Rollaway"),
    0x11: ("brake", EVENT_BRAKE_D),
    0x12: ("brake", EVENT_BRAKE_R),
    0x13: ("brake", "Rollaway"),
    0x14: ("brake", EVENT_BRAKE_EMERGENCY),
    0x21: ("system", EVENT_SYSTEM_RELEASE_DRIVER),
    0x22: ("system", EVENT_SYSTEM_RELEASE_P),
    0x23: ("system", EVENT_SYSTEM_RELEASE_MANUAL),
    0x24: ("system", EVENT_SYSTEM_RELEASE_RESET),
    0x30: ("system", EVENT_SYSTEM_STATUS_PERIODIC),
}
HEX_BYTE_RE = re.compile(r"^[0-9a-fA-F]{2}$")
BIN_CHUNK_RE = re.compile(r"^[01]+$")


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
    event_code INTEGER,
    status_word INTEGER,
    checksum_ok INTEGER,
    frame_format TEXT,
    raw_frame TEXT,
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
        existing_cols = {
            row["name"]
            for row in conn.execute("PRAGMA table_info(events)").fetchall()
        }
        alter_statements = [
            ("event_code", "ALTER TABLE events ADD COLUMN event_code INTEGER"),
            ("status_word", "ALTER TABLE events ADD COLUMN status_word INTEGER"),
            ("checksum_ok", "ALTER TABLE events ADD COLUMN checksum_ok INTEGER"),
            ("frame_format", "ALTER TABLE events ADD COLUMN frame_format TEXT"),
            ("raw_frame", "ALTER TABLE events ADD COLUMN raw_frame TEXT"),
        ]
        for column_name, sql in alter_statements:
            if column_name not in existing_cols:
                conn.execute(sql)

        existing_cols = {
            row["name"]
            for row in conn.execute("PRAGMA table_info(events)").fetchall()
        }
        if "event_code" in existing_cols:
            conn.execute("CREATE INDEX IF NOT EXISTS idx_events_code ON events(event_code)")
        conn.commit()


def now_str() -> str:
    try:
        return datetime.now(ZoneInfo(TIMEZONE_NAME)).strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


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
        insert_event(item, notify=False)
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
        "event_code": payload.get("event_code"),
        "status_word": payload.get("status_word"),
        "checksum_ok": normalize_bool(payload.get("checksum_ok")),
        "frame_format": normalize_text(payload.get("frame_format")) or None,
        "raw_frame": normalize_text(payload.get("raw_frame")) or None,
        "source": normalize_text(payload.get("source")) or "api",
        "raw_payload": json.dumps(payload, ensure_ascii=False),
        "received_at": now_str(),
    }
    return cleaned



def normalize_optional_int(value: Any) -> Optional[int]:
    if value is None or value == "":
        return None
    if isinstance(value, int):
        return value
    text = str(value).strip().lower()
    if text.startswith("0x"):
        try:
            return int(text, 16)
        except ValueError:
            return None
    try:
        return int(text)
    except ValueError:
        return None


def trim_events_if_needed(conn: sqlite3.Connection) -> None:
    if MAX_EVENTS <= 0:
        return
    total = conn.execute("SELECT COUNT(*) FROM events").fetchone()[0]
    overflow = int(total) - MAX_EVENTS
    if overflow <= 0:
        return
    conn.execute(
        """
        DELETE FROM events
        WHERE id IN (
            SELECT id FROM events
            ORDER BY event_time ASC, id ASC
            LIMIT ?
        )
        """,
        (overflow,),
    )


def push_event_stream_message(payload: Dict[str, Any]) -> None:
    data = json.dumps(payload, ensure_ascii=False)
    with _subscriber_lock:
        targets = list(_subscriber_queues)
    for q in targets:
        try:
            q.put_nowait(data)
        except queue.Full:
            try:
                _ = q.get_nowait()
            except queue.Empty:
                pass
            try:
                q.put_nowait(data)
            except queue.Full:
                pass


def insert_event(payload: Dict[str, Any], *, notify: bool = True) -> int:
    cleaned = validate_payload(payload)
    sql = """
    INSERT INTO events (
        event_time, event_category, event_type, gear_state, door_state,
        driver_present, vehicle_speed, event_code, status_word, checksum_ok,
        frame_format, raw_frame, source, raw_payload, received_at
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    """
    event_code = normalize_optional_int(cleaned.get("event_code"))
    status_word = normalize_optional_int(cleaned.get("status_word"))
    if status_word is not None:
        status_word &= 0xFFFF
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
                    event_code,
                    status_word,
                    cleaned["checksum_ok"],
                    cleaned["frame_format"],
                    cleaned["raw_frame"],
                    cleaned["source"],
                    cleaned["raw_payload"],
                    cleaned["received_at"],
                ),
            )
            trim_events_if_needed(conn)
            conn.commit()
            event_id = int(cur.lastrowid)

    if notify:
        live_payload = {
            "id": event_id,
            "event_time": cleaned["event_time"],
            "event_category": cleaned["event_category"],
            "event_type": cleaned["event_type"],
            "gear_state": cleaned["gear_state"],
            "door_state": cleaned["door_state"],
            "driver_present": cleaned["driver_present"],
            "vehicle_speed": cleaned["vehicle_speed"],
            "event_code": event_code,
            "event_code_hex": f"0x{event_code:02X}" if event_code is not None else None,
            "status_word": status_word,
            "status_word_hex": f"0x{status_word:04X}" if status_word is not None else None,
            "checksum_ok": cleaned["checksum_ok"],
            "frame_format": cleaned["frame_format"],
            "source": cleaned["source"],
            "received_at": cleaned["received_at"],
        }
        push_event_stream_message(live_payload)

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



def decode_status_word(status_word: int) -> Dict[str, Any]:
    status_word &= 0xFFFF
    warning_bits = (status_word >> 14) & 0b11
    brake_bits = (status_word >> 12) & 0b11
    gear_bits = (status_word >> 10) & 0b11
    door_bit = (status_word >> 9) & 0b1
    driver_bit = (status_word >> 8) & 0b1
    speed_kmh = status_word & 0xFF

    return {
        "warning_type": WARNING_TYPE_FROM_BITS.get(warning_bits),
        "brake_type": BRAKE_TYPE_FROM_BITS.get(brake_bits),
        "gear_state": GEAR_FROM_BITS.get(gear_bits, "UNKNOWN"),
        "door_state": DOOR_FROM_BIT.get(door_bit, "UNKNOWN"),
        "driver_present": int(driver_bit),
        "vehicle_speed": int(speed_kmh),
    }


def resolve_event_from_bits(event_code: Optional[int], parsed_status: Dict[str, Any]) -> tuple[str, str]:
    if event_code is not None and event_code in EVENT_CODE_MAP:
        return EVENT_CODE_MAP[event_code]

    brake_type = parsed_status.get("brake_type")
    warning_type = parsed_status.get("warning_type")
    if isinstance(brake_type, str) and brake_type:
        return "brake", brake_type
    if isinstance(warning_type, str) and warning_type:
        return "warning", warning_type
    return "system", EVENT_SYSTEM_STATUS_PERIODIC


def build_payload_from_status_word(
    *,
    status_word: int,
    raw_frame: str,
    frame_format: str,
    event_code: Optional[int] = None,
    checksum_ok: Optional[bool] = None,
) -> Dict[str, Any]:
    parsed_status = decode_status_word(status_word)
    event_category, event_type = resolve_event_from_bits(event_code, parsed_status)
    return {
        "event_time": now_str(),
        "event_category": event_category,
        "event_type": event_type,
        "gear_state": parsed_status["gear_state"],
        "door_state": parsed_status["door_state"],
        "driver_present": parsed_status["driver_present"],
        "vehicle_speed": parsed_status["vehicle_speed"],
        "event_code": event_code,
        "status_word": int(status_word & 0xFFFF),
        "checksum_ok": checksum_ok,
        "frame_format": frame_format,
        "raw_frame": raw_frame,
        "source": "serial",
    }


def parse_bytes_payload(byte_values: list[int], raw_frame: str) -> Optional[Dict[str, Any]]:
    if not byte_values:
        return None
    values = list(byte_values)
    framed = len(values) >= 5 and values[0] == FRAME_SOF and values[-1] == FRAME_EOF
    if FRAME_REQUIRE_MARKERS and not framed:
        return None
    if framed:
        values = values[1:-1]
        frame_format = "framed"
    else:
        frame_format = "plain"

    if len(values) == 1:
        event_code_only = values[0]
        if event_code_only in EVENT_CODE_MAP:
            return {"_event_code_only": event_code_only}
        return None

    if len(values) == 2:
        status_word = (values[0] << 8) | values[1]
        return build_payload_from_status_word(
            status_word=status_word,
            raw_frame=raw_frame,
            frame_format=f"{frame_format}_status2",
        )

    if len(values) == 3:
        event_code = values[0]
        status_word = (values[1] << 8) | values[2]
        return build_payload_from_status_word(
            event_code=event_code,
            status_word=status_word,
            raw_frame=raw_frame,
            frame_format=f"{frame_format}_evt_status",
        )

    if len(values) == 4:
        event_code = values[0]
        status_word = (values[1] << 8) | values[2]
        checksum = values[3]
        calculated = (event_code ^ values[1] ^ values[2]) & 0xFF
        checksum_ok = checksum == calculated
        if FRAME_REQUIRE_CHECKSUM and not checksum_ok:
            return None
        return build_payload_from_status_word(
            event_code=event_code,
            status_word=status_word,
            raw_frame=raw_frame,
            frame_format=f"{frame_format}_evt_status_checksum",
            checksum_ok=checksum_ok,
        )

    return None


def parse_binary_frame(line: str) -> Optional[Dict[str, Any]]:
    pieces = [p for p in re.split(r"[^01]+", line.strip()) if p]
    if not pieces:
        return None

    if len(pieces) == 1 and BIN_CHUNK_RE.fullmatch(pieces[0]):
        bits = pieces[0]
        if len(bits) == 8:
            value = int(bits, 2)
            if value in EVENT_CODE_MAP:
                return {"_event_code_only": value}
            return None
        if len(bits) == 16:
            return build_payload_from_status_word(
                status_word=int(bits, 2),
                raw_frame=line,
                frame_format="binary16",
            )
        if len(bits) == 24:
            event_code = int(bits[:8], 2)
            status_word = int(bits[8:], 2)
            return build_payload_from_status_word(
                event_code=event_code,
                status_word=status_word,
                raw_frame=line,
                frame_format="binary24",
            )
        if len(bits) == 32:
            byte_values = [int(bits[idx:idx + 8], 2) for idx in range(0, 32, 8)]
            return parse_bytes_payload(byte_values, line)
        return None

    if all(len(p) == 8 and BIN_CHUNK_RE.fullmatch(p) for p in pieces):
        byte_values = [int(p, 2) for p in pieces]
        return parse_bytes_payload(byte_values, line)
    return None


def parse_hex_or_decimal_bytes(line: str) -> Optional[Dict[str, Any]]:
    text = line.strip()
    if not text:
        return None

    pure_hex = re.sub(r"\s+", "", text)
    if len(pure_hex) >= 4 and len(pure_hex) % 2 == 0 and re.fullmatch(r"[0-9A-Fa-f]+", pure_hex):
        byte_values = [int(pure_hex[idx:idx + 2], 16) for idx in range(0, len(pure_hex), 2)]
        parsed = parse_bytes_payload(byte_values, line)
        if parsed:
            return parsed

    tokens = [tok for tok in re.split(r"[\s,;|]+", text) if tok]
    if not tokens:
        return None

    byte_values: list[int] = []
    for token in tokens:
        cleaned = token.strip().lower()
        if cleaned.startswith("0x"):
            cleaned = cleaned[2:]
        if HEX_BYTE_RE.fullmatch(cleaned):
            byte_values.append(int(cleaned, 16))
            continue
        if cleaned.isdigit():
            num = int(cleaned, 10)
            if 0 <= num <= 255:
                byte_values.append(num)
                continue
        return None

    if not byte_values:
        return None
    return parse_bytes_payload(byte_values, line)


def parse_event_code_only(line: str) -> Optional[int]:
    text = line.strip().lower()
    if not text:
        return None

    if BIN_CHUNK_RE.fullmatch(text):
        if len(text) == 8:
            value = int(text, 2)
            return value if value in EVENT_CODE_MAP else None
        if len(text) == 16:
            value = int(text, 2)
            if value <= 0xFF and value in EVENT_CODE_MAP:
                return value
            return None

    if text.startswith("0x"):
        text = text[2:]
    if HEX_BYTE_RE.fullmatch(text):
        value = int(text, 16)
        if value in EVENT_CODE_MAP:
            return value
    return None


def parse_serial_line(line: str, pending_event_code: Optional[int] = None) -> tuple[Optional[Dict[str, Any]], Optional[int]]:
    line = line.strip()
    if not line:
        return None, pending_event_code

    try:
        payload = json.loads(line)
        if isinstance(payload, dict):
            payload.setdefault("source", "serial")
            return payload, None
    except json.JSONDecodeError:
        pass

    parsed = parse_kv_message(line)
    if parsed:
        return parsed, None

    parsed = parse_pipe_message(line)
    if parsed:
        return parsed, None

    parsed = parse_binary_frame(line)
    if parsed:
        event_code_only = parsed.get("_event_code_only")
        if isinstance(event_code_only, int):
            return None, event_code_only
        if (
            pending_event_code is not None
            and parsed.get("event_code") is None
            and pending_event_code in EVENT_CODE_MAP
        ):
            event_category, event_type = EVENT_CODE_MAP[pending_event_code]
            parsed["event_code"] = pending_event_code
            parsed["event_category"] = event_category
            parsed["event_type"] = event_type
            parsed["frame_format"] = f"{parsed.get('frame_format', 'binary')}_paired"
        return parsed, None

    parsed = parse_hex_or_decimal_bytes(line)
    if parsed:
        event_code_only = parsed.get("_event_code_only")
        if isinstance(event_code_only, int):
            return None, event_code_only
        if (
            pending_event_code is not None
            and parsed.get("event_code") is None
            and pending_event_code in EVENT_CODE_MAP
        ):
            event_category, event_type = EVENT_CODE_MAP[pending_event_code]
            parsed["event_code"] = pending_event_code
            parsed["event_category"] = event_category
            parsed["event_type"] = event_type
            parsed["frame_format"] = f"{parsed.get('frame_format', 'bytes')}_paired"
        return parsed, None

    only_code = parse_event_code_only(line)
    if only_code is not None:
        return None, only_code

    return None, pending_event_code



def build_serial_signature(payload: Dict[str, Any]) -> str:
    comparable = {
        "category": payload.get("event_category"),
        "type": payload.get("event_type"),
        "gear": payload.get("gear_state"),
        "door": payload.get("door_state"),
        "driver": payload.get("driver_present"),
        "speed": payload.get("vehicle_speed"),
        "event_code": payload.get("event_code"),
        "status_word": payload.get("status_word"),
    }
    return json.dumps(comparable, ensure_ascii=False, sort_keys=True)


def should_skip_duplicate_serial(payload: Dict[str, Any]) -> bool:
    if SERIAL_DUPLICATE_WINDOW_SEC <= 0:
        return False

    signature = build_serial_signature(payload)
    now = time.time()

    with _serial_state_lock:
        global _last_serial_signature, _last_serial_saved_at
        within_window = (now - _last_serial_saved_at) <= SERIAL_DUPLICATE_WINDOW_SEC
        if within_window and signature == _last_serial_signature:
            return True
        _last_serial_signature = signature
        _last_serial_saved_at = now
    return False


def serial_ports_to_try() -> list[str]:
    ordered: list[str] = []
    for port in (SERIAL_PORT, *SERIAL_PORT_FALLBACKS):
        if port and port not in ordered:
            ordered.append(port)
    return ordered or [SERIAL_PORT]


def serial_worker() -> None:
    if not SERIAL_ENABLED:
        print("[SERIAL] disabled")
        return
    if serial is None:
        print("[SERIAL] pyserial is not installed.")
        return

    ports = serial_ports_to_try()
    while True:
        for port in ports:
            try:
                print(f"[SERIAL] opening: {port} @ {SERIAL_BAUDRATE}")
                with serial.Serial(port, SERIAL_BAUDRATE, timeout=1) as ser:
                    print(f"[SERIAL] connected: {port}")
                    pending_event_code: Optional[int] = None
                    pending_until = 0.0
                    while True:
                        raw_bytes = ser.readline()
                        if not raw_bytes:
                            if pending_event_code is not None and time.time() > pending_until:
                                pending_event_code = None
                            continue

                        raw = raw_bytes.decode("utf-8", errors="ignore").strip()
                        if not raw:
                            raw = " ".join(f"{b:02X}" for b in raw_bytes if b)
                            if not raw:
                                continue

                        payload, pending_event_code = parse_serial_line(raw, pending_event_code)
                        if payload is None:
                            if pending_event_code is not None:
                                pending_until = time.time() + 2.0
                            else:
                                print(f"[SERIAL] parse failed: {raw}")
                            continue

                        pending_event_code = None
                        if should_skip_duplicate_serial(payload):
                            continue

                        try:
                            event_id = insert_event(payload, notify=True)
                            print(f"[SERIAL] saved id={event_id} payload={payload}")
                        except Exception as exc:
                            print(f"[SERIAL] save failed: {exc} / raw={raw}")
            except Exception as exc:
                print(f"[SERIAL] connection error on {port}: {exc}")
        time.sleep(2)


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
    return jsonify({
        "status": "ok",
        "db_path": str(DB_PATH),
        "serial_enabled": SERIAL_ENABLED,
        "serial_port": SERIAL_PORT,
        "serial_ports": serial_ports_to_try(),
        "serial_baudrate": SERIAL_BAUDRATE,
        "serial_duplicate_window_sec": SERIAL_DUPLICATE_WINDOW_SEC,
        "max_events": MAX_EVENTS,
        "frame_sof_hex": f"0x{FRAME_SOF:02X}",
        "frame_eof_hex": f"0x{FRAME_EOF:02X}",
        "frame_require_markers": FRAME_REQUIRE_MARKERS,
        "frame_require_checksum": FRAME_REQUIRE_CHECKSUM,
        "send_url_enabled": SEND_URL_ENABLED,
        "now": now_str(),
    })


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


@app.route("/api/stream")
def api_stream():
    def generate():
        q: queue.Queue[str] = queue.Queue(maxsize=200)
        with _subscriber_lock:
            _subscriber_queues.add(q)
        hello = json.dumps({"type": "hello", "connected_at": now_str()}, ensure_ascii=False)
        yield f"event: hello\ndata: {hello}\n\n"

        try:
            while True:
                try:
                    message = q.get(timeout=EVENT_STREAM_HEARTBEAT_SEC)
                except queue.Empty:
                    heartbeat = json.dumps({"type": "ping", "time": now_str()}, ensure_ascii=False)
                    yield f"event: ping\ndata: {heartbeat}\n\n"
                    continue
                yield f"event: new_event\ndata: {message}\n\n"
        finally:
            with _subscriber_lock:
                _subscriber_queues.discard(q)

    headers = {
        "Cache-Control": "no-cache",
        "Connection": "keep-alive",
        "X-Accel-Buffering": "no",
    }
    return Response(stream_with_context(generate()), headers=headers, mimetype="text/event-stream")


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


@app.template_filter("yesno_or_dash")
def yesno_or_dash_filter(value: Any) -> str:
    if value is None or value == "":
        return "-"
    try:
        return "있음" if int(value) == 1 else "없음"
    except Exception:
        return str(value)


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


@app.template_filter("hex8")
def hex8_filter(value: Any) -> str:
    parsed = normalize_optional_int(value)
    if parsed is None:
        return "-"
    return f"0x{parsed & 0xFF:02X}"


@app.template_filter("hex16")
def hex16_filter(value: Any) -> str:
    parsed = normalize_optional_int(value)
    if parsed is None:
        return "-"
    return f"0x{parsed & 0xFFFF:04X}"



def start_background_threads() -> None:
    if SERIAL_ENABLED:
        t = threading.Thread(target=serial_worker, daemon=True)
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
