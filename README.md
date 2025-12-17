# 🤖 ESP32 Firmware — Table Tennis Training Robot Controller

This repository contains the **embedded firmware for an ESP32-based table tennis training robot**.  
The ESP32 operates as a **Wi-Fi Access Point** and **TCP server**, receiving JSON commands from a mobile application (Flutter) to control **multiple DC motors and a stepper motor** in real time.

This firmware is a **core part of an integrated system**, alongside a Flutter mobile app used as the control interface.

---

## 📌 Overview

The ESP32 firmware is responsible for:

- Hosting a **Wi-Fi Access Point (AP)**
- Running a **TCP server** for command reception
- Parsing **JSON-based control messages**
- Controlling:
  - **3 DC motors** via PWM (LEDC)
  - **1 stepper motor (NEMA17)** using an H-Bridge (L298N)
- Executing motor control logic using **FreeRTOS tasks**

---

## 🧠 System Architecture

```
Flutter Mobile App
        │
        │  Wi-Fi (AP Mode)
        │  TCP + JSON
        ▼
ESP32 (Access Point + TCP Server)
        │
        ├── DC Motor A (Launcher)
        ├── DC Motor B (Launcher)
        ├── DC Motor C (Launcher)
        └── Stepper Motor (Ball Feeder / Disc)
```

- The **ESP32 acts as the server**
- The **mobile app acts as the TCP client**
- Commands are sent as **JSON strings terminated by \n**

---

## 🧱 Hardware Components

| Component | Description |
|----------|-------------|
| ESP32 DevKit | Main microcontroller |
| L298N / L293D | H-Bridge motor driver |
| DC Motors (x3) | Ball launching motors |
| NEMA17 Stepper Motor | Ball feeding mechanism |
| External Power Supply (5–12V) | Motors and ESP32 |
| Jumpers & Wiring | Electrical connections |

---

## ⚙️ Pin Mapping

### DC Motors

#### Motor A
| Function | GPIO |
|--------|------|
| IN1 | GPIO 33 |
| IN2 | GPIO 32 |
| EN (PWM) | GPIO 14 |
| PWM Channel | LEDC_CHANNEL_0 |

#### Motor B
| Function | GPIO |
|--------|------|
| IN1 | GPIO 26 |
| IN2 | GPIO 27 |
| EN (PWM) | GPIO 25 |
| PWM Channel | LEDC_CHANNEL_1 |

#### Motor C
| Function | GPIO |
|--------|------|
| IN1 | GPIO 16 |
| IN2 | GPIO 17 |
| EN (PWM) | GPIO 18 |
| PWM Channel | LEDC_CHANNEL_2 |

---

### Stepper Motor (Motor 4)

| Function | GPIO |
|--------|------|
| IN1 | GPIO 4 |
| IN2 | GPIO 5 |
| IN3 | GPIO 19 |
| IN4 | GPIO 21 |

- Motor type: **NEMA17**
- Steps per revolution: **200**
- Disc cavities: **8**
- Steps per ball: **25**

---

## ⚙️ PWM & Timing Parameters

| Parameter | Value |
|---------|------|
| PWM Frequency | 5 kHz |
| PWM Resolution | 8-bit (0–255) |
| PWM Mode | LEDC Low Speed |
| Timer | LEDC_TIMER_0 |
| Stepper Min Delay | 5 ms |
| Stepper Max Delay | 40 ms |

---

## 📡 Wi-Fi & Network Configuration

The ESP32 runs in **Access Point mode**.

| Parameter | Value |
|----------|------|
| SSID | Robot |
| Password | 12345678 |
| IP Address | 192.168.4.1 |
| TCP Port | 8080 |
| Max Clients | 1 |

---

## 💬 Communication Protocol (JSON)

Commands are sent as JSON messages terminated with a newline (`\n`).

### DC Motor Control

```json
{"motor":1,"direction":"forward","speed":200}
{"motor":2,"direction":"stop"}
{"motor":3,"direction":"forward","speed":150}
```

### Stepper Motor Control (Motor 4)

**Continuous rotation**
```json
{"motor":4,"direction":"forward","speed":180}
```

**Balls per second**
```json
{"motor":4,"direction":"forward","bolinhas":3}
```

**Fixed interval between balls**
```json
{"motor":4,"direction":"forward","intervalo_ms":800}
```

### Stop Everything
```json
{"direction":"stop_all"}
```

---

## 🧩 Internal Architecture

### FreeRTOS Tasks

| Task | Responsibility |
|-----|---------------|
| tcp_server_task | TCP server and command parsing |
| stepper_task | Continuous stepper rotation |
| bolinha_task | Ball-per-second control |
| bolinha_intervalo_task | Fixed-interval feeding |

Only **one stepper mode runs at a time**, enforced via task control flags.

---

## 🧪 Testing the Firmware

1. Connect to Wi-Fi:
   ```
   SSID: Robot
   Password: 12345678
   ```

2. Open TCP client:
   ```bash
   nc 192.168.4.1 8080
   ```

3. Send command:
   ```json
   {"motor":1,"direction":"forward","speed":180}
   ```

4. Monitor logs:
   ```bash
   idf.py monitor
   ```

---

## ⚙️ Build & Flash

```bash
idf.py set-target esp32
idf.py build
idf.py flash
idf.py monitor
```

---

## 🎓 Academic Context

This firmware was developed as part of an **Integration Workshop** project, focusing on:

- Embedded systems
- Real-time motor control
- TCP/IP networking
- IoT system integration
- Hardware–software co-design

---

## 📄 License

This project is intended for **academic and educational use**.  
For commercial use, please contact the authors.


## 👨‍💻 Authors

Developed by students of the **Integration Workshop** course.

This project was developed and maintained with contributions from:

- **[@alfaGefersona](https://github.com/alfaGefersona)**
- **[@Tarsa-Reis](https://github.com/Tarsa-Reis)**
- **[@alfamatheuso](https://github.com/alfamatheuso)**
- **[@AlexandreMC23](https://github.com/AlexandreMC23)**

Thank you for your contributions and support in making this project possible! 🚀