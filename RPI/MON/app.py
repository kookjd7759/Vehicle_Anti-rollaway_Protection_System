import json
import os
import sqlite3
import threading
import time
from contextlib import closing
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, Optional
from zoneinfo import ZoneInfo

from flask import Flask, jsonify, redirect, render_template, request, url_for

try:
    import serial  # type: ignore
except Exception:
    serial = None

BASE_DIR = Path(__file__).resolve().parent
DB_PATH = Path(os.getenv("MON_DB_PATH", BASE_DIR / "mon_logs.db"))
SERIAL_PORT = os.getenv("MON_SERIAL_PORT", "/dev/serial0")
SERIAL_BAUDRATE = int(os.getenv("MON_SERIAL_BAUDRATE", "115200"))
SERIAL_ENABLED = os.getenv("MON_SERIAL_ENABLED", "1") == "1"
WEB_HOST = os.getenv("MON_WEB_HOST", "0.0.0.0")
WEB_PORT = int(os.getenv("MON_WEB_PORT", "5000"))
AUTO_REFRESH_SEC = int(os.getenv("MON_AUTO_REFRESH_SEC", "5"))
TIMEZONE_NAME = os.getenv("MON_TIMEZONE", "Asia/Seoul")

app = Flask(__name__)
app.json.ensure_ascii = False
_db_lock = threading.Lock()


VALID_CATEGORIES = {"warning", "brake", "system"}
VALID_WARNING_TYPES = {"1차", "강화", "Rollaway"}
VALID_BRAKE_TYPES = {"D단", "R단", "Rollaway", "긴급"}
VALID_GEAR = {"P", "R", "N", "D", "UNKNOWN", ""}
VALID_DOOR = {"OPEN", "CLOSED", "UNKNOWN", ""}


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


def build_sample_events(source: str = "sample") -> list[Dict[str, Any]]:
    try:
        base = datetime.now(ZoneInfo(TIMEZONE_NAME))
    except Exception:
        base = datetime.now()

    def ts(minutes_ago: int) -> str:
        return (base - timedelta(minutes=minutes_ago)).strftime("%Y-%m-%d %H:%M:%S")

    return [
        # warning events (FR-LOG-01)
        {"event_category": "warning", "event_type": "1차", "event_time": ts(42), "source": source},
        {"event_category": "warning", "event_type": "강화", "event_time": ts(39), "source": source},
        {"event_category": "warning", "event_type": "Rollaway", "event_time": ts(36), "source": source},
        {"event_category": "warning", "event_type": "1차", "event_time": ts(33), "source": source},
        {"event_category": "warning", "event_type": "강화", "event_time": ts(30), "source": source},
        # brake events (FR-LOG-02~06)
        {
            "event_category": "brake",
            "event_type": "D단",
            "event_time": ts(27),
            "gear_state": "D",
            "door_state": "OPEN",
            "driver_present": 0,
            "vehicle_speed": 7.9,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": "D단",
            "event_time": ts(24),
            "gear_state": "D",
            "door_state": "CLOSED",
            "driver_present": 0,
            "vehicle_speed": 5.8,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": "R단",
            "event_time": ts(21),
            "gear_state": "R",
            "door_state": "OPEN",
            "driver_present": 0,
            "vehicle_speed": 4.6,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": "R단",
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
            "event_type": "긴급",
            "event_time": ts(12),
            "gear_state": "D",
            "door_state": "OPEN",
            "driver_present": 1,
            "vehicle_speed": 9.5,
            "source": source,
        },
        {
            "event_category": "brake",
            "event_type": "긴급",
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
            "event_type": "제동 해제(운전자 복귀)",
            "event_time": ts(6),
            "gear_state": "P",
            "door_state": "CLOSED",
            "driver_present": 1,
            "vehicle_speed": 0,
            "source": source,
        },
        {
            "event_category": "system",
            "event_type": "제동 해제(P단 전환)",
            "event_time": ts(4),
            "gear_state": "P",
            "door_state": "CLOSED",
            "driver_present": 1,
            "vehicle_speed": 0,
            "source": source,
        },
        {
            "event_category": "system",
            "event_type": "이벤트 로그 전송 완료",
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
    text = normalize_text(value).lower()
    mapping = {
        "warn": "warning",
        "warning": "warning",
        "경고": "warning",
        "brake": "brake",
        "제동": "brake",
        "system": "system",
        "sys": "system",
        "시스템": "system",
    }
    return mapping.get(text, text)



def normalize_event_type(category: str, value: Any) -> str:
    text = normalize_text(value)
    if category == "warning":
        mapping = {
            "1": "1차",
            "1차경고": "1차",
            "1차 경고": "1차",
            "강화경고": "강화",
            "강화 경고": "강화",
            "rollaway": "Rollaway",
            "ROLLAWAY": "Rollaway",
        }
        return mapping.get(text, text)
    if category == "brake":
        mapping = {
            "d": "D단",
            "r": "R단",
            "rollaway": "Rollaway",
            "ROLLAWAY": "Rollaway",
            "emergency": "긴급",
            "긴급제동": "긴급",
            "긴급 제동": "긴급",
        }
        return mapping.get(text, text)
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
            return int(cur.lastrowid)



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
        return [dict(row) for row in rows]



def get_stats() -> Dict[str, Any]:
    with closing(get_db()) as conn:
        total = conn.execute("SELECT COUNT(*) FROM events").fetchone()[0]
        warning_count = conn.execute("SELECT COUNT(*) FROM events WHERE event_category='warning'").fetchone()[0]
        brake_count = conn.execute("SELECT COUNT(*) FROM events WHERE event_category='brake'").fetchone()[0]
        latest = conn.execute(
            "SELECT event_time, event_category, event_type FROM events ORDER BY event_time DESC, id DESC LIMIT 1"
        ).fetchone()
    return {
        "total": total,
        "warning_count": warning_count,
        "brake_count": brake_count,
        "latest": dict(latest) if latest else None,
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
        "serial_baudrate": SERIAL_BAUDRATE,
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
        t = threading.Thread(target=serial_worker, daemon=True)
        t.start()


if __name__ == "__main__":
    init_db()
    seeded_count = seed_sample_events_if_empty()
    if seeded_count:
        print(f"[SEED] inserted {seeded_count} sample events")
    start_background_threads()
    print(f"[WEB] http://{WEB_HOST}:{WEB_PORT}")
    app.run(host=WEB_HOST, port=WEB_PORT, debug=False)
