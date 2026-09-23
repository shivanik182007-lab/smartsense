import csv
import io
import json
import os
import secrets
import sqlite3
import time
from datetime import datetime, timedelta, timezone
from functools import wraps

from flask import (
    Flask,
    Response,
    jsonify,
    redirect,
    render_template,
    request,
    send_from_directory,
    session,
    url_for,
)
from flask_socketio import SocketIO
from werkzeug.security import check_password_hash, generate_password_hash


# ==========================================================
# PATHS
# ==========================================================

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

FIRMWARE_FOLDER = os.path.join(BASE_DIR, "firmware")
FIRMWARE_FILE = "firmware.bin"

DB_PATH = os.path.join(BASE_DIR, "sensor_history.db")

CONFIG_PATH = os.path.join(BASE_DIR, "config.json")
SENSOR_CONFIG_PATH = os.path.join(BASE_DIR, "sensor_config.json")

AUTH_CONFIG_PATH = os.path.join(BASE_DIR, "auth_config.json")
SECRET_KEY_PATH = os.path.join(BASE_DIR, "secret_key.txt")


# ==========================================================
# FLASK
# ==========================================================

app = Flask(__name__)

# A stable secret key is loaded from disk (generated once, on first
# run) rather than regenerated every start - otherwise every server
# restart would silently log everyone out of the dashboard mid-demo.
if os.path.exists(SECRET_KEY_PATH):
    with open(SECRET_KEY_PATH, "r", encoding="utf-8") as f:
        app.secret_key = f.read().strip()
else:
    app.secret_key = secrets.token_hex(32)
    with open(SECRET_KEY_PATH, "w", encoding="utf-8") as f:
        f.write(app.secret_key)

app.permanent_session_lifetime = timedelta(hours=8)

# threading async_mode needs no extra dependency (no eventlet/gevent
# required) - simplest option for a local network hackathon deployment.
socketio = SocketIO(
    app,
    cors_allowed_origins="*",
    async_mode="threading",
)


# ==========================================================
# GLOBAL STATE
# ==========================================================

update_available = False

current_version = "1.0"
latest_version = "1.0"

# Timestamp of the last time the ESP32 confirmed it actually applied
# a new firmware version (set inside /ack below). None until the
# first OTA update ever completes.
firmware_last_update = None

# --------------------------------------------------
# TEMPERATURE ALERT CONFIG
# --------------------------------------------------
# If a reading's temperature exceeds this value, an alert is pushed
# to all connected dashboards via Socket.IO. ALERT_COOLDOWN_SECONDS
# stops it from firing on every single reading while temp stays high.

TEMP_ALERT_THRESHOLD = 35.0  # degrees C - adjust for your demo
ALERT_COOLDOWN_SECONDS = 30

last_alert_time = {}  # sensor_id -> datetime of last alert sent

wifi_status = "Connected"
wifi_mode = "station"

ip_address = "10.153.121.191"
mac_address = "-"

last_sensor_status = "No Data"

ota_progress = {
    "value": 0,
    "uploaded": 0,
    "total": 0,
    "speed": 0.0,
}


# ==========================================================
# TIME
# ==========================================================

def utc_now():
    return datetime.now(timezone.utc).isoformat()


# ==========================================================
# DATABASE
# ==========================================================

def db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = db()

    conn.executescript(
        """
        CREATE TABLE IF NOT EXISTS sensors (
            sensor_id TEXT PRIMARY KEY,
            device_mac TEXT NOT NULL,
            sensor_name TEXT NOT NULL,
            sensor_type TEXT NOT NULL,
            gpio INTEGER,
            last_seen TEXT
        );

        CREATE TABLE IF NOT EXISTS readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sensor_id TEXT NOT NULL,
            timestamp TEXT NOT NULL,
            value REAL,
            status TEXT,
            temperature REAL,
            humidity REAL,
            FOREIGN KEY(sensor_id) REFERENCES sensors(sensor_id)
        );

        CREATE INDEX IF NOT EXISTS idx_readings_sensor_time
        ON readings(sensor_id, timestamp DESC);
        """
    )

    conn.commit()
    conn.close()


# ==========================================================
# JSON CONFIG
# ==========================================================

def ensure_json_file(path, default):
    if not os.path.exists(path):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(default, f, indent=4)


def load_json(path, default):
    ensure_json_file(path, default)

    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return default.copy() if isinstance(default, dict) else default


def save_json(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)


# ==========================================================
# INITIALIZATION
# ==========================================================

os.makedirs(FIRMWARE_FOLDER, exist_ok=True)

init_db()

ensure_json_file(
    CONFIG_PATH,
    {
        "ssid": "",
        "password": "",
        "server": "10.153.121.191",
        "port": "5000",
        "path": "/sensor",
        "ip_mode": "dhcp",
        "static_ip": "",
        "gateway": "",
        "subnet": "",
        "dns": "",
        "updated": False,
    },
)

ensure_json_file(
    SENSOR_CONFIG_PATH,
    {
        "sensor_name": "Sensor 1",
        "sensor_type": "IR",
        "gpio": 4,
        "pin_mode": "INPUT",
        "status": "enabled",
        "updated": False,
    },
)

# Default login is admin / smartsense123 - change it by editing
# auth_config.json (or deleting the file to fall back to this default
# again). The password is stored as a salted hash, never in plain text.
ensure_json_file(
    AUTH_CONFIG_PATH,
    {
        "username": "admin",
        "password_hash": generate_password_hash("smartsense123"),
    },
)


# ==========================================================
# AUTH
# ==========================================================

def login_required(view):
    """Protects browser-facing PAGE routes. Not logged in -> redirect
    to the login page. Never applied to ESP32-facing routes."""

    @wraps(view)
    def wrapped(*args, **kwargs):
        if not session.get("logged_in"):
            return redirect(url_for("login"))
        return view(*args, **kwargs)

    return wrapped


def api_login_required(view):
    """Protects browser-facing JSON/API routes. Not logged in -> 401
    JSON response (never a redirect - the caller is fetch(), not a
    full page navigation). Never applied to ESP32-facing routes."""

    @wraps(view)
    def wrapped(*args, **kwargs):
        if not session.get("logged_in"):
            return jsonify({"status": "error", "message": "Unauthorized"}), 401
        return view(*args, **kwargs)

    return wrapped


@app.route("/login", methods=["GET"])
def login():
    if session.get("logged_in"):
        return redirect("/dashboard")
    return render_template("login.html", error=None, username="")


@app.route("/login", methods=["POST"])
def login_submit():
    auth = load_json(AUTH_CONFIG_PATH, {})

    username = request.form.get("username", "").strip()
    password = request.form.get("password", "")

    valid = (
        username == auth.get("username", "")
        and password
        and check_password_hash(auth.get("password_hash", ""), password)
    )

    if not valid:
        return render_template("login.html", error="Invalid username or password.", username=username), 401

    session.permanent = True
    session["logged_in"] = True
    session["username"] = username

    return redirect("/dashboard")


@app.route("/logout", methods=["GET"])
def logout():
    session.clear()
    return redirect("/")


# ==========================================================
# FRONTEND
# ==========================================================

# Public landing/splash page - no login required. This is the
# first thing anyone hits at the site root. The "Open Dashboard"
# button on it links to /dashboard, which is login-protected as
# before - so unauthenticated visitors see the landing page here,
# then get sent to /login only once they actually try to enter.

@app.route("/", methods=["GET"])
def landing():
    return render_template("landing.html")


@app.route("/dashboard", methods=["GET"])
@app.route("/device", methods=["GET"])
@app.route("/sensor-page", methods=["GET"])
@app.route("/ota", methods=["GET"])
@app.route("/network", methods=["GET"])
@login_required
def home():
    return render_template(
        "index.html",
        current_version=current_version,
        latest_version=latest_version,
        wifi_status=wifi_status,
        ip_address=ip_address,
    )


# ==========================================================
# SENSOR DATA FROM ESP32
# ==========================================================

@app.route("/sensor", methods=["POST"])
def sensor():
    global mac_address
    global wifi_mode
    global last_sensor_status

    form = request.form

    mac = form.get("mac", "").strip()
    sensor_id = form.get("sensor_id", "").strip()
    sensor_name = form.get("sensor_name", "").strip()
    sensor_type = form.get("sensor_type", "").strip()
    gpio_text = form.get("gpio", "").strip()
    status = form.get("status", "").strip()
    mode = form.get("wifi_mode", "").strip()

    # Backward compatibility
    if not sensor_id:
        sensor_id = mac or "UNKNOWN"

    if not mac:
        if sensor_id.endswith("-S2"):
            mac = sensor_id[:-3]
        else:
            mac = sensor_id

    if not sensor_name:
        sensor_name = (
            "Sensor 2"
            if sensor_id.endswith("-S2")
            else "Sensor 1"
        )

    if not sensor_type:
        sensor_type = (
            "DHT22"
            if sensor_id.endswith("-S2")
            else "IR"
        )

    if mac:
        mac_address = mac

    if mode:
        wifi_mode = mode

    try:
        gpio = int(gpio_text) if gpio_text else None
    except (TypeError, ValueError):
        gpio = None

    def number(name):
        raw = form.get(name, "").strip()

        if raw == "":
            return None

        try:
            return float(raw)
        except (TypeError, ValueError):
            return None

    value = number("value")
    temperature = number("temperature")
    humidity = number("humidity")

    sensor_type_upper = sensor_type.upper()

    # ------------------------------------------------------
    # STATUS NORMALIZATION
    # ------------------------------------------------------

    if sensor_type_upper == "DHT22":

        if status:
            status_text = status

        elif temperature is not None and humidity is not None:
            status_text = "OK"

        else:
            status_text = "DHT22 No Data"

        if temperature is not None and humidity is not None:
            last_sensor_status = (
                f"{sensor_name}: "
                f"{temperature:.1f} C / "
                f"{humidity:.1f} %"
            )
        else:
            last_sensor_status = f"{sensor_name}: No Data"

    else:

        if status == "0":
            status_text = "Object Detected"

        elif status == "1":
            status_text = "No Object"

        elif value is not None and value == 0:
            status_text = "Object Detected"

        elif value is not None and value == 1:
            status_text = "No Object"

        else:
            status_text = status or "No Data"

        last_sensor_status = status_text

    # ------------------------------------------------------
    # SAVE
    # ------------------------------------------------------

    now = utc_now()

    conn = db()

    try:
        conn.execute(
            """
            INSERT INTO sensors
                (
                    sensor_id,
                    device_mac,
                    sensor_name,
                    sensor_type,
                    gpio,
                    last_seen
                )
            VALUES (?, ?, ?, ?, ?, ?)

            ON CONFLICT(sensor_id)
            DO UPDATE SET
                device_mac = excluded.device_mac,
                sensor_name = excluded.sensor_name,
                sensor_type = excluded.sensor_type,
                gpio = excluded.gpio,
                last_seen = excluded.last_seen
            """,
            (
                sensor_id,
                mac,
                sensor_name,
                sensor_type,
                gpio,
                now,
            ),
        )

        conn.execute(
            """
            INSERT INTO readings
                (
                    sensor_id,
                    timestamp,
                    value,
                    status,
                    temperature,
                    humidity
                )
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            (
                sensor_id,
                now,
                value,
                status_text,
                temperature,
                humidity,
            ),
        )

        conn.commit()

    except sqlite3.Error as exc:

        conn.rollback()

        print("DATABASE ERROR:", exc)

        return jsonify({
            "status": "error",
            "message": "Failed to save sensor reading",
        }), 500

    finally:
        conn.close()

    print("--------------------------------")
    print("Sensor ID     :", sensor_id)
    print("Sensor Name   :", sensor_name)
    print("Sensor Type   :", sensor_type)
    print("GPIO          :", gpio)

    if sensor_type_upper == "DHT22":
        print("Temperature   :", temperature, "C")
        print("Humidity      :", humidity, "%")
        print("Status        :", status_text)
    else:
        print("Value         :", value)
        print("Status        :", status_text)

    print("Last Seen     :", now)
    print("--------------------------------")

    # --------------------------------------------------
    # PUSH TO ALL CONNECTED DASHBOARDS (WEBSOCKET)
    # --------------------------------------------------
    # Fired only after the DB write above succeeds, so the browser
    # never gets told about a reading that wasn't actually saved.

    socketio.emit(
        "new_reading",
        {
            "sensor_id": sensor_id,
            "sensor_name": sensor_name,
            "sensor_type": sensor_type,
            "gpio": gpio,
            "value": value,
            "status": status_text,
            "temperature": temperature,
            "humidity": humidity,
            "timestamp": now,
        },
    )

    # --------------------------------------------------
    # TEMPERATURE ALERT CHECK
    # --------------------------------------------------
    # Fires only for DHT22 readings that actually have a temperature
    # value, and only once per ALERT_COOLDOWN_SECONDS per sensor, so
    # it doesn't spam the dashboard every ~3 seconds while temp stays
    # above the threshold.

    if temperature is not None and temperature > TEMP_ALERT_THRESHOLD:

        last_fired = last_alert_time.get(sensor_id)
        now_dt = datetime.utcnow()

        if (
            last_fired is None
            or (now_dt - last_fired).total_seconds() >= ALERT_COOLDOWN_SECONDS
        ):

            last_alert_time[sensor_id] = now_dt

            socketio.emit(
                "temp_alert",
                {
                    "sensor_id": sensor_id,
                    "sensor_name": sensor_name,
                    "temperature": temperature,
                    "threshold": TEMP_ALERT_THRESHOLD,
                    "timestamp": now,
                },
            )

    return jsonify({
        "status": "success",
        "sensor_id": sensor_id,
        "sensor_name": sensor_name,
        "sensor_type": sensor_type,
        "gpio": gpio,
        "timestamp": now,
    })


# ==========================================================
# SENSOR LIST
# ==========================================================

@app.route("/sensors", methods=["GET"])
@api_login_required
def sensors():
    conn = db()

    try:
        rows = conn.execute(
            """
            SELECT
                s.sensor_id,
                s.device_mac,
                s.sensor_name,
                s.sensor_type,
                s.gpio,
                s.last_seen,

                COUNT(r.id) AS data_points,

                (
                    SELECT r2.value
                    FROM readings r2
                    WHERE r2.sensor_id = s.sensor_id
                    ORDER BY r2.timestamp DESC, r2.id DESC
                    LIMIT 1
                ) AS latest_value,

                (
                    SELECT r2.status
                    FROM readings r2
                    WHERE r2.sensor_id = s.sensor_id
                    ORDER BY r2.timestamp DESC, r2.id DESC
                    LIMIT 1
                ) AS latest_status,

                (
                    SELECT r2.temperature
                    FROM readings r2
                    WHERE r2.sensor_id = s.sensor_id
                    ORDER BY r2.timestamp DESC, r2.id DESC
                    LIMIT 1
                ) AS latest_temperature,

                (
                    SELECT r2.humidity
                    FROM readings r2
                    WHERE r2.sensor_id = s.sensor_id
                    ORDER BY r2.timestamp DESC, r2.id DESC
                    LIMIT 1
                ) AS latest_humidity

            FROM sensors s

            LEFT JOIN readings r
                ON r.sensor_id = s.sensor_id

            GROUP BY
                s.sensor_id,
                s.device_mac,
                s.sensor_name,
                s.sensor_type,
                s.gpio,
                s.last_seen

            ORDER BY s.sensor_id
            """
        ).fetchall()

        return jsonify([dict(row) for row in rows])

    finally:
        conn.close()


# ==========================================================
# SENSOR HISTORY
# ==========================================================

@app.route("/sensor_history", methods=["GET"])
@api_login_required
def sensor_history():
    sensor_id = request.args.get(
        "sensor_id",
        ""
    ).strip()

    if not sensor_id:
        return jsonify({
            "error": "sensor_id is required"
        }), 400

    try:
        limit = int(
            request.args.get(
                "limit",
                "100"
            )
        )
    except (TypeError, ValueError):
        limit = 100

    limit = max(
        1,
        min(limit, 1000)
    )

    conn = db()

    try:

        exists = conn.execute(
            """
            SELECT 1
            FROM sensors
            WHERE sensor_id = ?
            LIMIT 1
            """,
            (sensor_id,),
        ).fetchone()

        if exists is None:
            return jsonify([])

        rows = conn.execute(
            """
            SELECT
                id,
                sensor_id,
                timestamp,
                value,
                status,
                temperature,
                humidity
            FROM readings
            WHERE sensor_id = ?
            ORDER BY timestamp DESC, id DESC
            LIMIT ?
            """,
            (
                sensor_id,
                limit,
            ),
        ).fetchall()

        return jsonify([
            dict(row)
            for row in reversed(rows)
        ])

    finally:
        conn.close()


@app.route("/history", methods=["GET"])
def history_alias():
    return sensor_history()


# ==========================================================
# EXPORT HISTORY (CSV)
# ==========================================================
# Downloads the reading history for ONE sensor as a CSV file.
# Same sensor_id/limit params as /sensor_history, just returned
# as a file download instead of JSON. Called from the "Export
# CSV" button next to the Points selector on the Dashboard -
# it always exports whichever sensor is currently selected
# there, so IR and DHT22 are downloaded separately.

@app.route("/export_history", methods=["GET"])
@api_login_required
def export_history():

    sensor_id = request.args.get(
        "sensor_id",
        ""
    ).strip()

    if not sensor_id:
        return jsonify({
            "error": "sensor_id is required"
        }), 400

    try:
        limit = int(
            request.args.get(
                "limit",
                "100"
            )
        )
    except (TypeError, ValueError):
        limit = 100

    limit = max(
        1,
        min(limit, 1000)
    )

    conn = db()

    try:

        sensor_row = conn.execute(
            """
            SELECT sensor_name, sensor_type
            FROM sensors
            WHERE sensor_id = ?
            LIMIT 1
            """,
            (sensor_id,),
        ).fetchone()

        if sensor_row is None:
            return jsonify({
                "error": "Unknown sensor_id"
            }), 404

        rows = conn.execute(
            """
            SELECT
                timestamp,
                value,
                status,
                temperature,
                humidity
            FROM readings
            WHERE sensor_id = ?
            ORDER BY timestamp DESC, id DESC
            LIMIT ?
            """,
            (
                sensor_id,
                limit,
            ),
        ).fetchall()

    finally:
        conn.close()

    sensor_name = sensor_row["sensor_name"]
    sensor_type = sensor_row["sensor_type"]

    # IR sensors never have temperature/humidity data, so those
    # columns are left out of the CSV entirely for them - matches
    # the history table on the Dashboard, which hides them too.
    include_temp_humidity = sensor_type != "IR"

    # Build the CSV in memory - no temp file needed.
    output = io.StringIO()
    writer = csv.writer(output)

    header = ["timestamp", "value", "status"]

    if include_temp_humidity:
        header.extend(["temperature", "humidity"])

    writer.writerow(header)

    for row in reversed(rows):

        data_row = [
            row["timestamp"],
            row["value"],
            row["status"],
        ]

        if include_temp_humidity:
            data_row.extend([
                row["temperature"],
                row["humidity"],
            ])

        writer.writerow(data_row)

    csv_data = output.getvalue()
    output.close()

    safe_name = sensor_name.replace(" ", "_")
    filename = "{}_{}_history.csv".format(
        safe_name,
        sensor_type,
    )

    return Response(
        csv_data,
        mimetype="text/csv",
        headers={
            "Content-Disposition": "attachment; filename={}".format(filename)
        },
    )


# ==========================================================
# HEALTH
# ==========================================================

@app.route("/health", methods=["GET"])
def health():
    return jsonify({
        "status": "ok",
        "server_time": utc_now(),
    })


# ==========================================================
# STATUS
# ==========================================================

@app.route("/status", methods=["GET"])
@api_login_required
def status():

    sensor_config = load_json(
        SENSOR_CONFIG_PATH,
        {}
    )

    conn = db()

    try:
        rows = conn.execute(
            """
            SELECT
                sensor_id,
                device_mac,
                sensor_name,
                sensor_type,
                gpio,
                last_seen
            FROM sensors
            ORDER BY sensor_id
            """
        ).fetchall()

        connected_sensors = [
            dict(row)
            for row in rows
        ]

    finally:
        conn.close()

    # ESP32 normally sends every ~10 seconds.
    # Consider it online if a reading was received recently.
    online = False

    for item in connected_sensors:
        last_seen = item.get("last_seen")

        if not last_seen:
            continue

        try:
            last_dt = datetime.fromisoformat(
                last_seen.replace(
                    "Z",
                    "+00:00"
                )
            )

            age = (
                datetime.now(timezone.utc)
                - last_dt
            ).total_seconds()

            if age <= 30:
                online = True
                break

        except (ValueError, TypeError):
            pass

    return jsonify({
        "online": online,

        "sensor_status": last_sensor_status,

        "current_version": current_version,
        "latest_version": latest_version,

        "last_update": firmware_last_update,

        "wifi_status": wifi_status,
        "wifi_mode": wifi_mode,

        "ip_address": ip_address,
        "mac_address": mac_address,

        "update_available": update_available,

        "sensor_name": sensor_config.get(
            "sensor_name",
            "Sensor 1"
        ),

        "sensor_type": sensor_config.get(
            "sensor_type",
            "IR"
        ),

        "gpio": sensor_config.get(
            "gpio",
            4
        ),

        "pin_mode": sensor_config.get(
            "pin_mode",
            "INPUT"
        ),

        "ap_ip": "192.168.4.1",

        "ota_progress": ota_progress,

        "connected_sensors": connected_sensors,
    })


# ==========================================================
# OTA
# ==========================================================

@app.route(
    "/enable_update",
    methods=["GET", "POST"]
)
@api_login_required
def enable_update():

    global update_available

    update_available = True

    return jsonify({
        "status": "success",
        "update_available": True,
    })


@app.route("/check_update", methods=["GET"])
def check_update():
    return (
        "UPDATE"
        if update_available
        else "NO_UPDATE"
    )


@app.route("/firmware.bin", methods=["GET"])
def firmware():

    firmware_path = os.path.join(
        FIRMWARE_FOLDER,
        FIRMWARE_FILE
    )

    if not os.path.isfile(firmware_path):
        return "Firmware file not available", 404

    return send_from_directory(
        FIRMWARE_FOLDER,
        FIRMWARE_FILE,
        as_attachment=True,
        download_name=FIRMWARE_FILE,
    )


@app.route("/ack", methods=["POST"])
def ack():

    global update_available
    global current_version
    global latest_version
    global firmware_last_update

    if update_available:
        current_version = latest_version
        update_available = False
        firmware_last_update = utc_now()

    return "OK"


@app.route("/upload", methods=["POST"])
@api_login_required
def upload():

    global update_available
    global latest_version

    file = request.files.get(
        "firmware"
    )

    if file is None:
        return jsonify({
            "status": "error",
            "message": "Missing firmware file",
        }), 400

    filename = (
        file.filename or ""
    ).lower()

    if not filename.endswith(".bin"):
        return jsonify({
            "status": "error",
            "message": "Only .bin firmware files are allowed",
        }), 400

    os.makedirs(
        FIRMWARE_FOLDER,
        exist_ok=True
    )

    save_path = os.path.join(
        FIRMWARE_FOLDER,
        FIRMWARE_FILE
    )

    uploaded = 0
    start_time = time.time()

    with open(save_path, "wb") as f:

        while True:

            chunk = file.stream.read(
                4096
            )

            if not chunk:
                break

            f.write(chunk)

            uploaded += len(chunk)

    elapsed = max(
        time.time() - start_time,
        0.001
    )

    speed = (
        uploaded /
        elapsed /
        1024
    )

    ota_progress["value"] = 100
    ota_progress["uploaded"] = uploaded
    ota_progress["total"] = uploaded
    ota_progress["speed"] = speed

    try:
        latest_version = str(
            round(
                float(current_version) + 0.1,
                1
            )
        )
    except (TypeError, ValueError):
        latest_version = current_version

    update_available = True

    print("Firmware Upload Complete")
    print(
        f"Size : {uploaded / 1024:.1f} KB"
    )
    print(
        f"Speed: {speed:.1f} KB/s"
    )

    return jsonify({
        "status": "success",
        "message": "Firmware uploaded successfully.",
        "size": uploaded,
        "speed": speed,
        "version": latest_version,
    })


@app.route(
    "/ota_progress",
    methods=["GET"]
)
def ota_progress_update():

    try:
        ota_progress["value"] = max(
            0,
            min(
                100,
                int(
                    request.args.get(
                        "value",
                        "0"
                    )
                )
            )
        )
    except (TypeError, ValueError):
        ota_progress["value"] = 0

    try:
        ota_progress["uploaded"] = max(
            0,
            int(
                request.args.get(
                    "uploaded",
                    "0"
                )
            )
        )
    except (TypeError, ValueError):
        ota_progress["uploaded"] = 0

    try:
        ota_progress["total"] = max(
            0,
            int(
                request.args.get(
                    "total",
                    "0"
                )
            )
        )
    except (TypeError, ValueError):
        ota_progress["total"] = 0

    try:
        ota_progress["speed"] = max(
            0.0,
            float(
                request.args.get(
                    "speed",
                    "0"
                )
            )
        )
    except (TypeError, ValueError):
        ota_progress["speed"] = 0.0

    return jsonify(ota_progress)


# ==========================================================
# NETWORK CONFIGURATION
# ==========================================================

@app.route("/network_config", methods=["GET"])
@api_login_required
def network_config():
    return jsonify(
        load_json(
            CONFIG_PATH,
            {}
        )
    )


@app.route("/network_update", methods=["GET"])
def network_update():

    config = load_json(
        CONFIG_PATH,
        {}
    )

    return (
        "UPDATE"
        if config.get("updated", False)
        else "NO_UPDATE"
    )


@app.route("/network_ack", methods=["GET", "POST"])
def network_ack():

    config = load_json(
        CONFIG_PATH,
        {}
    )

    config["updated"] = False

    save_json(
        CONFIG_PATH,
        config
    )

    return "OK"


@app.route("/save_network", methods=["POST"])
@api_login_required
def save_network():

    incoming = (
        request.get_json(
            silent=True
        )
        or {}
    )

    data = load_json(
        CONFIG_PATH,
        {}
    )

    if not isinstance(data, dict):
        data = {}

    data.update(incoming)
    data["updated"] = True

    save_json(
        CONFIG_PATH,
        data
    )

    return jsonify({
        "status": "success",
        "updated": True,
    })


# ==========================================================
# SENSOR CONFIGURATION
# ==========================================================

@app.route("/sensor_config", methods=["GET"])
@api_login_required
def sensor_config():
    return jsonify(
        load_json(
            SENSOR_CONFIG_PATH,
            {}
        )
    )


@app.route("/sensor_update", methods=["GET"])
def sensor_update():

    config = load_json(
        SENSOR_CONFIG_PATH,
        {}
    )

    return (
        "UPDATE"
        if config.get("updated", False)
        else "NO_UPDATE"
    )


@app.route("/sensor_ack", methods=["GET", "POST"])
def sensor_ack():

    config = load_json(
        SENSOR_CONFIG_PATH,
        {}
    )

    config["updated"] = False

    save_json(
        SENSOR_CONFIG_PATH,
        config
    )

    return "OK"


@app.route("/save_sensor", methods=["POST"])
@api_login_required
def save_sensor():

    incoming = (
        request.get_json(
            silent=True
        )
        or {}
    )

    data = load_json(
        SENSOR_CONFIG_PATH,
        {}
    )

    if not isinstance(data, dict):
        data = {}

    data.update(incoming)
    data["updated"] = True

    save_json(
        SENSOR_CONFIG_PATH,
        data
    )

    return jsonify({
        "status": "success",
        "updated": True,
    })


# ==========================================================
# STATIC FILES
# ==========================================================

@app.route("/style.css", methods=["GET"])
def style():
    return send_from_directory(
        os.path.join(
            BASE_DIR,
            "static"
        ),
        "style.css"
    )


@app.route("/script.js", methods=["GET"])
def script():
    return send_from_directory(
        os.path.join(
            BASE_DIR,
            "static"
        ),
        "script.js"
    )


# ==========================================================
# START
# ==========================================================

if __name__ == "__main__":
    # socketio.run replaces app.run so the WebSocket layer starts
    # correctly alongside the normal HTTP routes. Same host/port/debug
    # as before - nothing else about how the server starts has changed.
    socketio.run(
        app,
        host="0.0.0.0",
        port=5000,
        debug=False
    )