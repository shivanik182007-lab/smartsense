# SmartSense

SmartSense is an IoT-based real-time environmental monitoring system designed to collect, process, and visualize sensor data using an ESP32 and a web-based monitoring platform.

The system integrates a DHT22 temperature and humidity sensor and an IR sensor with an ESP32. Sensor data is transmitted over Wi-Fi to a Flask-based backend, where it is processed, stored, and displayed through a web dashboard.

## Overview

SmartSense provides a centralized platform for monitoring environmental parameters and device activity in real time. The system combines embedded firmware, wireless communication, backend processing, database storage, and a web interface into a single IoT solution.

## Key Features

* Real-time temperature and humidity monitoring
* IR-based object detection
* Real-time sensor data updates
* Historical sensor data visualization
* Temperature alerts
* Sensor data storage using SQLite
* CSV data export
* User authentication
* Device status monitoring
* Last-update tracking
* Remote network configuration
* Over-the-air firmware updates
* WebSocket-based real-time communication
* Responsive web dashboard

## Technology Stack

### Hardware

* ESP32
* DHT22 Temperature and Humidity Sensor
* IR Sensor

### Firmware

* C++
* Arduino Framework
* PlatformIO

### Backend

* Python
* Flask
* Flask-SocketIO
* SQLite

### Frontend

* HTML
* CSS
* JavaScript
* WebSocket

## System Architecture

```text
DHT22 Sensor ──┐
               │
IR Sensor ─────┤
               ▼
             ESP32
               │
               │ Wi-Fi
               ▼
        Flask Backend
               │
       ┌───────┴────────┐
       │                │
       ▼                ▼
    SQLite          WebSocket
    Database            │
                        ▼
                Web Dashboard
```

## How It Works

1. The DHT22 sensor measures temperature and humidity.
2. The IR sensor detects the presence of an object.
3. The ESP32 collects the sensor readings.
4. Sensor data is transmitted to the Flask backend over Wi-Fi.
5. The backend processes and stores the received data in SQLite.
6. WebSocket communication provides real-time updates to the dashboard.
7. Users can view current readings, historical data, device status, and alerts through the web interface.

## Dashboard

The SmartSense dashboard provides access to:

* Current temperature
* Current humidity
* IR detection status
* Device connectivity status
* Last sensor update
* Historical sensor readings
* Temperature alerts
* Sensor data export

## Device Management

SmartSense includes device-management functionality for:

* Network configuration
* Wi-Fi settings
* Backend server configuration
* Device status monitoring
* OTA firmware updates

## Project Structure

```text
smartsense/
│
├── backend/
│   ├── app.py
│   ├── templates/
│   │   ├── index.html
│   │   ├── landing.html
│   │   └── login.html
│   │
│   └── static/
│       ├── script.js
│       └── style.css
│
├── firmware/
│   ├── main.cpp
│   ├── platformio.ini
│   ├── .gitignore
│   └── README
│
├── .gitignore
└── README.md
```

## Configuration

Sensitive configuration files are excluded from version control.

Example configuration files are provided in the backend directory:

```text
backend/config.example.json
backend/auth_config.example.json
backend/secret_key.example.txt
```

These example files can be used as templates for creating the required local configuration files.

Do not commit Wi-Fi credentials, authentication credentials, secret keys, or other sensitive information to the repository.

## Installation and Setup

### Clone the Repository

```bash
git clone https://github.com/shivanik182007-lab/smartsense.git
cd smartsense
```

### Backend

Navigate to the backend directory:

```bash
cd backend
```

Create the required local configuration files using the provided example files and install the required Python dependencies.

Start the Flask application:

```bash
python app.py
```

### Firmware

Open the `firmware` directory using PlatformIO.

Configure the required device settings and upload the firmware to the ESP32.

```text
firmware/
├── main.cpp
└── platformio.ini
```

## Project Objectives

SmartSense demonstrates the integration of embedded systems, IoT communication, real-time data processing, database management, and web technologies into a unified monitoring platform.

The project aims to provide a simple and centralized interface for monitoring sensor data while also supporting device management and remote firmware updates.

## Hackathon

SmartSense is being developed for the ORION 1.0 Hackathon under the Open Innovation track.



**Project:** SmartSense
