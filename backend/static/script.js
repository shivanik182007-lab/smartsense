"use strict";


/* =========================================================
   API
   ========================================================= */

const API = {

    sensors: "/sensors",

    history: "/sensor_history",

    status: "/status",

    health: "/health",

    networkConfig: "/network_config",

    sensorConfig: "/sensor_config",

    ota: "/upload"

};


/* =========================================================
   LIVE UPDATES (WEBSOCKET)
   ========================================================= */
// Pushes new sensor readings to the dashboard the instant they
// arrive, instead of waiting for the next 10-second poll. The
// setInterval polling further down is left in place on purpose,
// as a fallback in case the socket connection drops - it just
// won't usually be the thing that updates the screen anymore.

let liveSocket = null;


function initLiveUpdates() {

    if (
        typeof io ===
        "undefined"
    ) {

        console.warn(
            "Socket.IO client not loaded - live updates disabled, falling back to polling only."
        );

        return;
    }


    liveSocket = io();


    liveSocket.on(
        "connect",
        () => {

            console.log(
                "Live updates connected"
            );

        }
    );


    liveSocket.on(
        "disconnect",
        () => {

            console.warn(
                "Live updates disconnected - falling back to polling until reconnected"
            );

        }
    );


    liveSocket.on(
        "new_reading",
        () => {

            // A reading was just saved on the server. Reuse the
            // same refresh functions the polling timers already
            // call, just triggered instantly instead of on a timer.

            loadSensors(true);

            refreshStatus();

        }
    );


    liveSocket.on(
        "temp_alert",
        (data) => {

            showTempAlert(data);

        }
    );

}


// Downloads the CSV for whichever sensor is currently selected in
// the Sensor dropdown (IR or DHT22) - reuses the same sensor_id
// and limit the chart/history table is already showing.

function exportSensorHistory() {

    if (!selectedSensorId) {

        alert(
            "Select a sensor first."
        );

        return;
    }

    const limit = $("limitSelect")
        ? $("limitSelect").value
        : "100";

    const url =
        "/export_history?sensor_id=" +
        encodeURIComponent(selectedSensorId) +
        "&limit=" +
        encodeURIComponent(limit);

    window.location.href = url;

}


let tempAlertTimeout = null;


function showTempAlert(data) {

    const banner = document.getElementById(
        "tempAlertBanner"
    );

    if (!banner) {

        console.warn(
            "tempAlertBanner element not found in HTML"
        );

        return;
    }

    banner.textContent =
        "\u26A0 High Temperature: " +
        data.temperature +
        "\u00B0C on " +
        data.sensor_name +
        " (threshold " +
        data.threshold +
        "\u00B0C)";

    banner.style.display = "block";

    clearTimeout(
        tempAlertTimeout
    );

    tempAlertTimeout = setTimeout(
        () => {

            banner.style.display = "none";

        },
        8000
    );

}


/* =========================================================
   GLOBAL STATE
   ========================================================= */

let sensors = [];

let selectedSensorId = "";

let refreshTimer = null;

let statusTimer = null;

let historyRequestId = 0;


/* =========================================================
   DOM
   ========================================================= */

function $(id) {
    return document.getElementById(id);
}


/* =========================================================
   HELPERS
   ========================================================= */

function escapeHtml(value) {

    return String(value ?? "")
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
}


function setHidden(id, hidden) {

    const element = $(id);

    if (!element) {
        return;
    }

    element.classList.toggle(
        "hidden",
        hidden
    );
}


function formatDate(value) {

    if (!value) {
        return "--";
    }

    const date = new Date(value);

    if (Number.isNaN(date.getTime())) {
        return String(value);
    }

    return date.toLocaleString();
}


function numberOrNull(value) {

    if (
        value === null ||
        value === undefined ||
        value === ""
    ) {
        return null;
    }

    const number = Number(value);

    return Number.isFinite(number)
        ? number
        : null;
}


function showMessage(
    message,
    type = "info"
) {

    const box = $("systemMessage");

    if (!box) {
        return;
    }

    if (!message) {

        box.textContent = "";

        box.className =
            "message hidden";

        return;
    }

    box.textContent = message;

    box.className =
        "message " + type;
}


/* =========================================================
   FETCH JSON
   ========================================================= */

async function fetchJson(
    url,
    options = {}
) {

    const response = await fetch(
        url,
        {
            cache: "no-store",
            ...options
        }
    );

    // Session expired / not logged in - bounce to the login page
    // instead of leaving the dashboard stuck on a silent 401.
    if (response.status === 401) {
        window.location.href = "/login";
        return new Promise(() => {});
    }

    const text =
        await response.text();

    let data = {};

    try {

        data = text
            ? JSON.parse(text)
            : {};

    } catch (_) {

        data = {
            raw: text
        };

    }

    if (!response.ok) {

        throw new Error(
            data?.message ||
            data?.error ||
            data?.raw ||
            (
                "HTTP " +
                response.status +
                " " +
                response.statusText
            )
        );

    }

    return data;
}


/* =========================================================
   CLIENT-SIDE NAVIGATION
   ========================================================= */

function showPage(page) {

    const target =
        $("page-" + page);

    if (!target) {

        console.error(
            "Page not found:",
            page
        );

        return false;
    }


    document
        .querySelectorAll(".page")
        .forEach(
            element => {
                element.classList.remove(
                    "active-page"
                );
            }
        );


    document
        .querySelectorAll(".nav-btn")
        .forEach(
            button => {
                button.classList.remove(
                    "active"
                );
            }
        );


    target.classList.add(
        "active-page"
    );


    const button =
        document.querySelector(
            '.nav-btn[data-page="' +
            page +
            '"]'
        );


    if (button) {
        button.classList.add(
            "active"
        );
    }


    if (page === "dashboard") {

        refreshDashboard();

    } else if (page === "device") {

        refreshStatus();
        loadSensors(true);

    } else if (page === "sensor") {

        loadSensorConfiguration();

    } else if (page === "network") {

        refreshStatus();
        loadNetworkConfiguration();

    }


    return false;
}


/* =========================================================
   SENSOR NORMALIZATION
   ========================================================= */

function normalizeSensor(sensor) {

    return {

        sensor_id:
            sensor.sensor_id ??
            sensor.id ??
            "",

        device_mac:
            sensor.device_mac ??
            sensor.mac ??
            "",

        sensor_name:
            sensor.sensor_name ??
            sensor.name ??
            "Sensor",

        sensor_type:
            String(
                sensor.sensor_type ??
                sensor.type ??
                "IR"
            ).toUpperCase(),

        gpio:
            sensor.gpio ??
            sensor.pin ??
            "--",

        last_seen:
            sensor.last_seen ??
            sensor.updated_at ??
            null,

        data_points:
            sensor.data_points ??
            sensor.count ??
            0,

        latest_value:
            sensor.latest_value ??
            sensor.value ??
            null,

        latest_status:
            sensor.latest_status ??
            sensor.status ??
            null,

        latest_temperature:
            sensor.latest_temperature ??
            sensor.temperature ??
            null,

        latest_humidity:
            sensor.latest_humidity ??
            sensor.humidity ??
            null

    };
}


/* =========================================================
   LOAD SENSORS
   ========================================================= */

async function loadSensors(
    preserveSelection = true
) {

    try {

        const data =
            await fetchJson(
                API.sensors
            );


        if (Array.isArray(data)) {

            sensors =
                data.map(
                    normalizeSensor
                );

        } else if (
            Array.isArray(
                data.sensors
            )
        ) {

            sensors =
                data.sensors.map(
                    normalizeSensor
                );

        } else {

            sensors = [];

        }


        const previous =
            preserveSelection
                ? selectedSensorId
                : "";


        if (
            previous &&
            sensors.some(
                sensor =>
                    sensor.sensor_id === previous
            )
        ) {

            selectedSensorId =
                previous;

        } else if (
            sensors.length
        ) {

            selectedSensorId =
                sensors[0].sensor_id;

        } else {

            selectedSensorId = "";

        }


        renderSensorSelector();

        renderDeviceSensors();

        updateSelectedSensor();


        if (selectedSensorId) {
            await loadHistory();
        } else {
            clearHistory();
        }


        return sensors;

    } catch (error) {

        console.error(
            "loadSensors:",
            error
        );

        sensors = [];

        renderSensorSelector();

        clearDashboard();

        showMessage(
            "Could not load sensors: " +
            error.message,
            "error"
        );

        return [];

    }
}


/* =========================================================
   SENSOR SELECTOR
   ========================================================= */

function renderSensorSelector() {

    const select =
        $("sensorSelect");

    if (!select) {
        return;
    }


    select.innerHTML = "";


    if (!sensors.length) {

        select.innerHTML =
            '<option value="">No sensors available</option>';

        return;
    }


    sensors.forEach(
        sensor => {

            const option =
                document.createElement(
                    "option"
                );

            option.value =
                sensor.sensor_id;

            option.textContent =
                sensor.sensor_name +
                " (" +
                sensor.sensor_type +
                " / GPIO " +
                sensor.gpio +
                ")";

            select.appendChild(
                option
            );

        }
    );


    select.value =
        selectedSensorId;
}


/* =========================================================
   SELECTED SENSOR
   ========================================================= */

function getSelectedSensor() {

    return sensors.find(
        sensor =>
            sensor.sensor_id ===
            selectedSensorId
    ) || null;
}


function handleSensorSelection() {

    const select =
        $("sensorSelect");

    if (!select) {
        return;
    }

    selectedSensorId =
        select.value;

    updateSelectedSensor();

    loadHistory();
}


function updateSelectedSensor() {

    const sensor =
        getSelectedSensor();

    if (!sensor) {

        clearDashboard();

        return;
    }


    $("sensorName").textContent =
        sensor.sensor_name;

    $("sensorId").textContent =
        "ID: " +
        sensor.sensor_id;

    $("sensorType").textContent =
        sensor.sensor_type;

    $("sensorGPIO").textContent =
        sensor.gpio;

    $("lastSeen").textContent =
        formatDate(
            sensor.last_seen
        );

    $("dataPoints").textContent =
        sensor.data_points ?? 0;

    $("selectedSensorTitle").textContent =
        "Sensor: " +
        sensor.sensor_name;


    updateCurrentReading(
        sensor
    );
}


/* =========================================================
   CLEAR DASHBOARD
   ========================================================= */

function clearDashboard() {

    if ($("sensorName")) {
        $("sensorName").textContent = "--";
    }

    if ($("sensorId")) {
        $("sensorId").textContent = "ID: --";
    }

    if ($("sensorType")) {
        $("sensorType").textContent = "--";
    }

    if ($("sensorGPIO")) {
        $("sensorGPIO").textContent = "--";
    }

    if ($("lastSeen")) {
        $("lastSeen").textContent = "--";
    }

    if ($("dataPoints")) {
        $("dataPoints").textContent = "0";
    }

    setHidden(
        "irReading",
        true
    );

    setHidden(
        "dhtReading",
        true
    );

    setHidden(
        "emptyReading",
        false
    );

    clearHistory();
}


/* =========================================================
   CURRENT READING
   ========================================================= */

function updateCurrentReading(
    sensor
) {

    const type =
        sensor.sensor_type;


    setHidden(
        "irReading",
        type !== "IR"
    );

    setHidden(
        "dhtReading",
        type !== "DHT22"
    );

    setHidden(
        "emptyReading",
        type !== "IR" &&
        type !== "DHT22"
    );


    if (type === "IR") {

        let status =
            sensor.latest_status;


        if (!status) {

            const value =
                numberOrNull(
                    sensor.latest_value
                );

            if (value === 0) {
                status =
                    "Object Detected";
            } else if (value === 1) {
                status =
                    "No Object";
            } else {
                status = "--";
            }

        }


        $("irStatus").textContent =
            status;


        const indicator =
            $("readingIndicator");


        if (indicator) {

            indicator.className =
                "reading-indicator";


            if (
                String(status)
                    .toLowerCase()
                    .includes("detected")
            ) {

                indicator.classList.add(
                    "detected"
                );

            } else {

                indicator.classList.add(
                    "clear"
                );

            }

        }


        $("currentReadingDescription")
            .textContent =
            sensor.latest_value === null ||
            sensor.latest_value === undefined
                ? "No digital value reported."
                : "Digital value: " +
                  sensor.latest_value;

    }


    if (type === "DHT22") {

        const temperature =
            numberOrNull(
                sensor.latest_temperature
            );

        const humidity =
            numberOrNull(
                sensor.latest_humidity
            );


        $("temperature").textContent =
            temperature === null
                ? "-- °C"
                : temperature.toFixed(1) +
                  " °C";


        $("humidity").textContent =
            humidity === null
                ? "-- %"
                : humidity.toFixed(1) +
                  " %";

    }

}


/* =========================================================
   DASHBOARD REFRESH
   ========================================================= */

async function refreshDashboard() {

    await loadSensors(true);

    await refreshStatus();
}


async function refreshAll() {

    await loadSensors(true);

    await refreshStatus();
}


/* =========================================================
   HISTORY
   ========================================================= */

async function loadHistory() {

    if (!selectedSensorId) {

        clearHistory();

        return;
    }


    const requestId =
        ++historyRequestId;


    const limit =
        Number(
            $("limitSelect")?.value ||
            50
        );


    const url =
        API.history +
        "?sensor_id=" +
        encodeURIComponent(
            selectedSensorId
        ) +
        "&limit=" +
        encodeURIComponent(
            limit
        );


    try {

        const data =
            await fetchJson(url);


        if (
            requestId !==
            historyRequestId
        ) {
            return;
        }


        let rows =
            Array.isArray(data)
                ? data
                : (
                    Array.isArray(
                        data.readings
                    )
                        ? data.readings
                        : (
                            Array.isArray(
                                data.history
                            )
                                ? data.history
                                : []
                        )
                );


        rows =
            rows.map(
                row => ({

                    timestamp:
                        row.timestamp ??
                        row.time ??
                        row.created_at ??
                        null,

                    value:
                        row.value ?? null,

                    status:
                        row.status ?? null,

                    temperature:
                        row.temperature ?? null,

                    humidity:
                        row.humidity ?? null

                })
            );


        renderHistoryTable(
            rows
        );

        drawChart(
            rows
        );


    } catch (error) {

        console.error(
            "loadHistory:",
            error
        );

        $("historyBody").innerHTML =
            '<tr><td colspan="5">' +
            "Could not load history: " +
            escapeHtml(
                error.message
            ) +
            "</td></tr>";

        clearChart();

    }

}


/* =========================================================
   HISTORY TABLE
   ========================================================= */

function renderHistoryTable(
    rows
) {

    const body =
        $("historyBody");

    if (!body) {
        return;
    }


    const sensor =
        getSelectedSensor();

    const showTempHumidity =
        sensor?.sensor_type !==
        "IR";

    setHidden(
        "thTemperature",
        !showTempHumidity
    );

    setHidden(
        "thHumidity",
        !showTempHumidity
    );

    const colCount =
        showTempHumidity
            ? 5
            : 3;


    body.innerHTML = "";


    if (!rows.length) {

        body.innerHTML =
            '<tr><td colspan="' +
            colCount +
            '">' +
            "No history available." +
            "</td></tr>";

        setHidden(
            "emptyHistory",
            false
        );

        return;
    }


    setHidden(
        "emptyHistory",
        true
    );


    [...rows]
        .reverse()
        .forEach(
            row => {

                const tr =
                    document.createElement(
                        "tr"
                    );


                const values = [

                    formatDate(
                        row.timestamp
                    ),

                    row.value === null ||
                    row.value === undefined
                        ? "--"
                        : row.value,

                    row.status === null ||
                    row.status === undefined
                        ? "--"
                        : row.status

                ];


                if (showTempHumidity) {

                    values.push(

                        numberOrNull(
                            row.temperature
                        ) === null
                            ? "--"
                            : numberOrNull(
                                row.temperature
                            ).toFixed(1) +
                              " °C",

                        numberOrNull(
                            row.humidity
                        ) === null
                            ? "--"
                            : numberOrNull(
                                row.humidity
                            ).toFixed(1) +
                              " %"

                    );

                }


                values.forEach(
                    value => {

                        const td =
                            document.createElement(
                                "td"
                            );

                        td.textContent =
                            value;

                        tr.appendChild(
                            td
                        );

                    }
                );


                body.appendChild(
                    tr
                );

            }
        );

}


/* =========================================================
   CHART
   ========================================================= */

function clearHistory() {

    if ($("historyBody")) {

        $("historyBody").innerHTML =
            '<tr><td colspan="5">' +
            "No sensor selected." +
            "</td></tr>";

    }

    clearChart();
}


function clearChart() {

    const canvas =
        $("chart");

    if (!canvas) {
        return;
    }

    const ctx =
        canvas.getContext("2d");

    ctx.clearRect(
        0,
        0,
        canvas.width,
        canvas.height
    );

    setHidden(
        "emptyHistory",
        false
    );
}


function drawChart(rows) {

    const canvas =
        $("chart");

    if (!canvas) {
        return;
    }


    const ctx =
        canvas.getContext("2d");


    if (!rows.length) {

        clearChart();

        return;
    }


    const rect =
        canvas.getBoundingClientRect();


    const width =
        Math.max(
            300,
            Math.floor(
                rect.width
            )
        );


    const height =
        Math.max(
            220,
            Math.floor(
                rect.height ||
                280
            )
        );


    const ratio =
        window.devicePixelRatio ||
        1;


    canvas.width =
        width * ratio;

    canvas.height =
        height * ratio;


    ctx.setTransform(
        ratio,
        0,
        0,
        ratio,
        0,
        0
    );


    ctx.clearRect(
        0,
        0,
        width,
        height
    );


    const sensor =
        getSelectedSensor();


    const type =
        sensor?.sensor_type ||
        "IR";


    const points =
        rows
            .map(
                row =>
                    type === "DHT22"
                        ? numberOrNull(
                            row.temperature
                        )
                        : numberOrNull(
                            row.value
                        )
            )
            .filter(
                value =>
                    value !== null
            );


    if (!points.length) {

        ctx.font =
            "14px Arial";

        ctx.fillText(
            "No numeric history available",
            20,
            30
        );

        setHidden(
            "emptyHistory",
            false
        );

        return;
    }


    setHidden(
        "emptyHistory",
        true
    );


    const left = 55;
    const right = 20;
    const top = 20;
    const bottom = 40;


    const chartWidth =
        width -
        left -
        right;


    const chartHeight =
        height -
        top -
        bottom;


    let minimum =
        Math.min(...points);


    let maximum =
        Math.max(...points);


    if (minimum === maximum) {

        minimum -= 1;
        maximum += 1;

    }


    const range =
        maximum - minimum;


    ctx.strokeStyle =
        "#dbe3ef";

    ctx.lineWidth = 1;


    for (
        let i = 0;
        i <= 4;
        i++
    ) {

        const y =
            top +
            chartHeight *
            i /
            4;


        ctx.beginPath();

        ctx.moveTo(
            left,
            y
        );

        ctx.lineTo(
            width - right,
            y
        );

        ctx.stroke();


        const label =
            maximum -
            range *
            i /
            4;


        ctx.fillStyle =
            "#64748b";

        ctx.font =
            "11px Arial";

        ctx.fillText(
            label.toFixed(1),
            5,
            y + 4
        );

    }


    ctx.strokeStyle =
        "#2563d8";

    ctx.fillStyle =
        "#2563d8";

    ctx.lineWidth = 2;


    ctx.beginPath();


    points.forEach(
        (value, index) => {

            const x =
                left +
                chartWidth *
                index /
                Math.max(
                    1,
                    points.length - 1
                );


            const y =
                top +
                chartHeight -
                (
                    (
                        value -
                        minimum
                    ) /
                    range
                ) *
                chartHeight;


            if (index === 0) {

                ctx.moveTo(
                    x,
                    y
                );

            } else {

                ctx.lineTo(
                    x,
                    y
                );

            }

        }
    );


    ctx.stroke();


    points.forEach(
        (value, index) => {

            const x =
                left +
                chartWidth *
                index /
                Math.max(
                    1,
                    points.length - 1
                );


            const y =
                top +
                chartHeight -
                (
                    (
                        value -
                        minimum
                    ) /
                    range
                ) *
                chartHeight;


            ctx.beginPath();

            ctx.arc(
                x,
                y,
                3,
                0,
                Math.PI * 2
            );

            ctx.fill();

        }
    );


    ctx.fillStyle =
        "#475569";

    ctx.font =
        "12px Arial";


    ctx.fillText(
        type === "DHT22"
            ? "Temperature (°C)"
            : "IR Value",
        left,
        height - 12
    );

}


/* =========================================================
   STATUS
   ========================================================= */

async function refreshStatus() {

    try {

        const data =
            await fetchJson(
                API.status
            );

        updateStatusUI(
            data
        );

        return data;

    } catch (error) {

        console.error(
            "refreshStatus:",
            error
        );

        setOfflineStatus(
            error.message
        );

        return null;

    }

}


function updateStatusUI(data) {

    const online =
        data.online === true;


    const ip =
        data.ip_address ||
        "--";


    const mac =
        data.mac_address ||
        "--";


    const wifi =
        data.wifi_status ||
        "--";


    const version =
        data.current_version ||
        "--";


    const lastUpdate =
        data.last_update
            ? formatDate(data.last_update)
            : "--";


    $("ipAddress").textContent =
        ip;

    $("cardIpAddress").textContent =
        ip;

    $("deviceMac").textContent =
        mac;

    $("devicePageIp").textContent =
        ip;

    $("devicePageMac").textContent =
        mac;

    $("wifiStatus").textContent =
        wifi;

    $("networkIp").textContent =
        ip;

    $("networkMac").textContent =
        mac;

    $("networkWifi").textContent =
        wifi;

    $("version").textContent =
        version;

    $("sidebarVersion").textContent =
        "Version: " +
        version;

    $("lastUpdate").textContent =
        lastUpdate;


    $("serverAddress").textContent =
        window.location.origin;


    $("deviceStatus").textContent =
        online
            ? "Online"
            : "Offline";


    $("deviceStatusMessage").textContent =
        online
            ? "ESP32 is reporting sensor data."
            : "No recent ESP32 sensor data.";


    $("deviceConnectionValue").textContent =
        online
            ? "Online"
            : "Offline";


    if (online) {

        setOnlineStatus();

    } else {

        setOfflineStatus();

    }

}


function setOnlineStatus() {

    $("deviceOnline").className =
        "status-dot online";

    $("deviceOnlineText").textContent =
        "Device online";

}


function setOfflineStatus(reason = "") {

    $("deviceOnline").className =
        "status-dot offline";

    $("deviceOnlineText").textContent =
        reason
            ? "Device offline: " + reason
            : "Device offline";

}


/* =========================================================
   DEVICE SENSOR LIST
   ========================================================= */

function renderDeviceSensors() {

    const box =
        $("deviceSensorsList");

    if (!box) {
        return;
    }


    if (!sensors.length) {

        box.textContent =
            "No sensors found.";

        return;
    }


    box.innerHTML =
        sensors
            .map(
                sensor => `

                    <div class="sensor-list-item">

                        <div>
                            <strong>
                                ${escapeHtml(
                                    sensor.sensor_name
                                )}
                            </strong>

                            <span>
                                ${escapeHtml(
                                    sensor.sensor_type
                                )}
                            </span>
                        </div>

                        <div>
                            GPIO
                            ${escapeHtml(
                                sensor.gpio
                            )}
                        </div>

                        <div>
                            ${escapeHtml(
                                sensor.sensor_id
                            )}
                        </div>

                    </div>

                `
            )
            .join("");

}


/* =========================================================
   SENSOR CONFIGURATION
   ========================================================= */

async function loadSensorConfiguration() {

    const box =
        $("sensorConfigList");

    if (!box) {
        return;
    }


    box.textContent =
        "Loading...";


    try {

        const data =
            await fetchJson(
                API.sensors
            );


        const list =
            Array.isArray(data)
                ? data.map(normalizeSensor)
                : [];


        if (!list.length) {

            box.textContent =
                "No sensor configuration available.";

            return;
        }


        box.innerHTML =
            list
                .map(
                    sensor => `

                        <div class="config-card">

                            <div class="config-header">

                                <h3>
                                    ${escapeHtml(
                                        sensor.sensor_name
                                    )}
                                </h3>

                                <span class="badge">
                                    ${escapeHtml(
                                        sensor.sensor_type
                                    )}
                                </span>

                            </div>


                            <div class="info-list">

                                <div>

                                    <span>
                                        Sensor ID
                                    </span>

                                    <strong>
                                        ${escapeHtml(
                                            sensor.sensor_id
                                        )}
                                    </strong>

                                </div>


                                <div>

                                    <span>
                                        Device MAC
                                    </span>

                                    <strong>
                                        ${escapeHtml(
                                            sensor.device_mac ||
                                            "--"
                                        )}
                                    </strong>

                                </div>


                                <div>

                                    <span>
                                        GPIO
                                    </span>

                                    <strong>
                                        ${escapeHtml(
                                            sensor.gpio
                                        )}
                                    </strong>

                                </div>


                                <div>

                                    <span>
                                        Data Points
                                    </span>

                                    <strong>
                                        ${escapeHtml(
                                            sensor.data_points
                                        )}
                                    </strong>

                                </div>


                                <div>

                                    <span>
                                        Last Seen
                                    </span>

                                    <strong>
                                        ${escapeHtml(
                                            formatDate(
                                                sensor.last_seen
                                            )
                                        )}
                                    </strong>

                                </div>

                            </div>

                        </div>

                    `
                )
                .join("");


    } catch (error) {

        console.error(
            "loadSensorConfiguration:",
            error
        );

        box.textContent =
            "Could not load sensor configuration: " +
            error.message;

    }

}


/* =========================================================
   NETWORK CONFIGURATION
   ========================================================= */

async function loadNetworkConfiguration() {

    try {

        const data =
            await fetchJson(
                API.networkConfig
            );


        const box =
            $("networkConfigList");


        if (!box) {
            return;
        }


        const fields = [

            ["SSID", data.ssid],

            ["IP Mode", data.ip_mode],

            ["Static IP", data.static_ip],

            ["Gateway", data.gateway],

            ["Subnet", data.subnet],

            ["DNS", data.dns],

            ["Server", data.server],

            ["Port", data.port],

            ["Path", data.path]

        ];


        box.innerHTML =
            fields
                .map(
                    ([name, value]) => `

                        <div>

                            <span>
                                ${escapeHtml(name)}
                            </span>

                            <strong>
                                ${escapeHtml(
                                    value || "--"
                                )}
                            </strong>

                        </div>

                    `
                )
                .join("");


        const setValue = (id, value) => {

            const el = $(id);

            if (el) {
                el.value = value || "";
            }

        };

        setValue(
            "networkMode",
            data.mode === "ap" ? "ap" : "station"
        );
        setValue("networkSsid", data.ssid);
        setValue("networkPassword", data.password);
        setValue(
            "networkIpMode",
            data.ip_mode === "static" ? "static" : "dhcp"
        );
        setValue("networkStaticIp", data.static_ip);
        setValue("networkGateway", data.gateway);
        setValue("networkSubnet", data.subnet);
        setValue("networkDns", data.dns);
        setValue("networkServer", data.server);
        setValue("networkPort", data.port);
        setValue("networkPath", data.path);

        toggleNetworkMode();
        toggleStaticNetworkFields();


    } catch (error) {

        console.error(
            "loadNetworkConfiguration:",
            error
        );

        const box =
            $("networkConfigList");

        if (box) {

            box.textContent =
                "Could not load network configuration: " +
                error.message;

        }

    }

}


function toggleNetworkMode() {

    const modeSelect =
        $("networkMode");

    const ssidLabel =
        $("networkSsidLabel");

    const passwordLabel =
        $("networkPasswordLabel");

    if (!modeSelect) {
        return;
    }

    const isAp =
        modeSelect.value === "ap";

    if (ssidLabel) {
        ssidLabel.textContent =
            isAp
                ? "Access Point Name (SSID)"
                : "Wi-Fi SSID";
    }

    if (passwordLabel) {
        passwordLabel.textContent =
            isAp
                ? "Access Point Password"
                : "Wi-Fi Password";
    }

}


function toggleStaticNetworkFields() {

    const ipModeSelect =
        $("networkIpMode");

    const isStatic =
        !!ipModeSelect &&
        ipModeSelect.value === "static";

    setHidden("staticNetworkFields", !isStatic);

}


async function saveNetworkConfiguration(event) {

    if (event) {
        event.preventDefault();
    }

    const message =
        $("networkFormMessage");

    const payload = {
        mode: $("networkMode")
            ? $("networkMode").value
            : "station",

        ssid: $("networkSsid")
            ? $("networkSsid").value.trim()
            : "",

        password: $("networkPassword")
            ? $("networkPassword").value
            : "",

        ip_mode: $("networkIpMode")
            ? $("networkIpMode").value
            : "dhcp",

        static_ip: $("networkStaticIp")
            ? $("networkStaticIp").value.trim()
            : "",

        gateway: $("networkGateway")
            ? $("networkGateway").value.trim()
            : "",

        subnet: $("networkSubnet")
            ? $("networkSubnet").value.trim()
            : "",

        dns: $("networkDns")
            ? $("networkDns").value.trim()
            : "",

        server: $("networkServer")
            ? $("networkServer").value.trim()
            : "",

        port: $("networkPort")
            ? $("networkPort").value.trim()
            : "",

        path: $("networkPath")
            ? $("networkPath").value.trim()
            : ""
    };


    if (message) {
        message.textContent = "Saving...";
        message.className = "form-message";
    }


    try {

        const response =
            await fetch(
                "/save_network",
                {
                    method: "POST",
                    headers: {
                        "Content-Type": "application/json"
                    },
                    body: JSON.stringify(payload)
                }
            );

        if (!response.ok) {
            throw new Error(
                "Server returned " + response.status
            );
        }

        if (message) {
            message.textContent =
                "Saved. The ESP32 will apply this on its next network check.";
            message.className = "form-message success";
        }

        await loadNetworkConfiguration();

    } catch (error) {

        console.error(
            "saveNetworkConfiguration:",
            error
        );

        if (message) {
            message.textContent =
                "Save failed: " + error.message;
            message.className = "form-message error";
        }

    }

}


/* =========================================================
   OTA
   ========================================================= */

function submitOTA(event) {

    if (event) {

        event.preventDefault();
        event.stopPropagation();

    }


    const input =
        $("firmwareFile");

    const progress =
        $("otaProgress");

    const status =
        $("otaStatusText");


    if (
        !input ||
        !input.files.length
    ) {

        showOtaMessage(
            "Please select a firmware .bin file.",
            "error"
        );

        return false;
    }


    const file =
        input.files[0];


    if (
        !file.name
            .toLowerCase()
            .endsWith(".bin")
    ) {

        showOtaMessage(
            "Please select a .bin firmware file.",
            "error"
        );

        return false;
    }


    const formData =
        new FormData();


    formData.append(
        "firmware",
        file
    );


    showOtaMessage(
        "Uploading firmware...",
        "info"
    );


    progress.style.width =
        "0%";

    progress.textContent =
        "0%";


    status.textContent =
        "Uploading " +
        file.name +
        "...";


    const xhr =
        new XMLHttpRequest();


    xhr.open(
        "POST",
        API.ota,
        true
    );


    xhr.upload.addEventListener(
        "progress",
        event => {

            if (!event.lengthComputable) {
                return;
            }


            const percent =
                Math.round(
                    event.loaded /
                    event.total *
                    100
                );


            progress.style.width =
                percent + "%";

            progress.textContent =
                percent + "%";


            status.textContent =
                "Uploaded " +
                formatBytes(
                    event.loaded
                ) +
                " / " +
                formatBytes(
                    event.total
                );

        }
    );


    xhr.onload = function() {

        let data = null;


        try {

            data =
                JSON.parse(
                    xhr.responseText
                );

        } catch (_) {

            data = null;

        }


        if (
            xhr.status >= 200 &&
            xhr.status < 300
        ) {

            progress.style.width =
                "100%";

            progress.textContent =
                "100%";


            showOtaMessage(
                data?.message ||
                "Firmware uploaded successfully.",
                "success"
            );


            status.textContent =
                "Firmware upload completed.";

        } else {

            showOtaMessage(
                "OTA upload failed: " +
                (
                    data?.message ||
                    data?.error ||
                    xhr.responseText ||
                    "HTTP " + xhr.status
                ),
                "error"
            );


            status.textContent =
                "OTA failed.";

        }

    };


    xhr.onerror = function() {

        showOtaMessage(
            "Network error while uploading firmware.",
            "error"
        );

        status.textContent =
            "Network error.";

    };


    xhr.send(
        formData
    );


    return false;
}


function showOtaMessage(
    message,
    type
) {

    const box =
        $("otaMessage");

    if (!box) {
        return;
    }

    box.textContent =
        message;

    box.className =
        "message " + type;
}


function formatBytes(bytes) {

    if (
        !Number.isFinite(bytes)
    ) {
        return "--";
    }


    if (bytes < 1024) {

        return bytes + " B";

    }


    if (
        bytes <
        1024 * 1024
    ) {

        return (
            bytes / 1024
        ).toFixed(1) +
        " KB";

    }


    return (
        bytes /
        1024 /
        1024
    ).toFixed(2) +
    " MB";
}


/* =========================================================
   RESIZE
   ========================================================= */

window.addEventListener(
    "resize",
    () => {

        if (
            getSelectedSensor() &&
            $("chart")
        ) {

            loadHistory();

        }

    }
);


/* =========================================================
   DOM READY
   ========================================================= */

document.addEventListener(
    "DOMContentLoaded",
    () => {

        const select =
            $("sensorSelect");


        if (select) {

            select.addEventListener(
                "change",
                handleSensorSelection
            );

        }


        const form =
            $("otaForm");


        if (form) {

            form.addEventListener(
                "submit",
                submitOTA
            );

        }


        const networkForm =
            $("networkForm");


        if (networkForm) {

            networkForm.addEventListener(
                "submit",
                saveNetworkConfiguration
            );

        }


        loadSensors(false);

        refreshStatus();

        loadNetworkConfiguration();

        initLiveUpdates();


        refreshTimer =
            setInterval(
                () => {

                    loadSensors(true);

                },
                10000
            );


        statusTimer =
            setInterval(
                () => {

                    refreshStatus();

                },
                10000
            );


        showPage(
            "dashboard"
        );

    }
);


/* =========================================================
   ERROR LOGGING
   ========================================================= */

window.addEventListener(
    "error",
    event => {

        console.error(
            "Dashboard JavaScript error:",
            event.error ||
            event.message
        );

    }
);


window.addEventListener(
    "unhandledrejection",
    event => {

        console.error(
            "Dashboard promise error:",
            event.reason
        );

    }
);