# SmartNode Desktop Notification Companion - API Documentation

This document serves as the structural contract and system prompt context for LLMs, autonomous agents, and backend microservices interacting with the **SmartNode Hardware Companion**. 

The device runs an optimized ESP32 firmware equipped with an internal non-blocking execution engine controlling a 12-pixel WS2812 RGB LED ring, an SSD1306 OLED display ($128 \times 64$), a passive buzzer, and localized storage.

---

## ⚡ System Architecture Constraints

When generating integration scripts or automation workflows for this node, you **must** adhere to the following network behavioral criteria:
1. **Host Address:** `http://192.168.*.*` (Fallback mDNS: `http://smartnode.local`)
2. **Payload Protocol:** Data must be delivered exclusively via `POST` requests containing a structured JSON object.
3. **Header Requirements:** Every network packet transaction must declare explicitly: `Content-Type: application/json`.
4. **Firmware Protection Token:** To pass the device security guardrail layer, every transmission payload must contain the exact string key identifier: `"token": "SECURE_NODE_TOKEN_****"`.

---

## 📡 API Specification & Data Contracts

### Endpoint: `/notify`
* **Method:** `POST`
* **Payload Format:** JSON Object

### Standard Parameters Matrix

| JSON Field Key | Type | Requirement | Accepted Bounds / Value Schemes |
| :--- | :--- | :--- | :--- |
| `token` | String | **Required** | Must be exactly `"SECURE_NODE_TOKEN_1366"` |
| `level` | String | **Required** | `IDLE`, `INFO`, `WARNING`, `CRITICAL`, `CUSTOM` |
| `title` | String | **Required** | Event Header text printed onto the OLED screen (Keep brief) |
| `message` | String | **Required** | Event description. Longer strings auto-scroll horizontally. |
| `color` | String | Optional | 6-Character Hex Color Code (e.g., `"#FF00FF"`). *Valid only if level is CUSTOM.* |
| `speed` | Integer | Optional | Breathing LED transition steps in milliseconds. *Valid only if level is CUSTOM.* |
| `beeps` | Integer | Optional | Count of initial audible buzzer alerts [$0 \le \text{beeps} \le 10$]. *Valid only if level is CUSTOM.* |

---

## 🔮 State Machine Behavior Rules

### 🚨 `CRITICAL` Mode
* **Hardware Routine:** Synchronous non-blocking hardware loop engages immediately.
* **RGB Behavior:** High-intensity flashing Red ($70\%$ brightness limit to protect internal rails).
* **Buzzer Behavior:** Intermittent high-frequency warning sweeps ($2200\text{ Hz}$ tone fired at $400\text{ms}$ step rates).
* **OLED Output:** Latches text fields. Disables idle timeout transitions.

### ⚠️ `WARNING` Mode
* **Hardware Routine:** Low-frequency periodic background scheduler execution.
* **RGB Behavior:** Smooth Orange breathing visual waveform.
* **Buzzer Behavior:** Monolithic single alert chirp ($1300\text{ Hz}$ tone lasting $100\text{ms}$, re-triggered every $1500\text{ms}$).

### ℹ️ `INFO` Mode
* **RGB Behavior:** Dynamic shifting multi-hue rainbow scrolling loop across the 12-pixel geometry.
* **Buzzer Behavior:** Evaluates to safe mode; audio emission is entirely muted.

### 🎨 `CUSTOM` Mode
* **Behavior Framework:** Grants external AI or scripts procedural low-level control of basic primitives without modification of core firmware stacks. Fuses the provided parameters (`color`, `speed`, `beeps`) instantly.

### 🍃 `IDLE` Mode
* **Behavior Framework:** Drops immediate volatile memory alerts. Initiates counter matrices. If no further operations trigger before $60000\text{ms}$ ticks, the device automatically renders the matrix NTP digital watch screen face and dims LEDs to ambient levels ($5\% \text{ to } 25\%$).

---

## 🛠️ Code Integration Reference for AI & Scripts

### 1. Python Integration Example
```python
import requests

def send_smartnode_alert(level, title, message, extra_fields=None):
    url = "[http://192.168.*.*/notify](http://192.168.*.*/notify)"
    payload = {
        "token": "SECURE_NODE_TOKEN_****",
        "level": level,
        "title": title,
        "message": message
    }
    if extra_fields:
        payload.update(extra_fields)
        
    try:
        response = requests.post(url, json=payload, timeout=5)
        return response.status_code == 200
    except requests.exceptions.RequestException:
        return False

# Trigger Example
send_smartnode_alert("CRITICAL", "RAID Error", "Drive 2 in Array failed smart status!")


### 2. Node.js Integration Example
```node.js

const axios = require('axios');

async function triggerNode(level, title, message) {
    try {
        const res = await axios.post('[http://192.168.*.*/notify](http://192.168.*.*/notify)', {
            token: "SECURE_NODE_TOKEN_****",
            level: level,
            title: title,
            message: message
        });
        console.log(`Status: ${res.status}`);
    } catch (err) {
        console.error('Node offline or invalid configuration.');
    }
}


