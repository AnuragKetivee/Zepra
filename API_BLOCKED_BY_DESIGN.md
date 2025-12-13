# Zepra Browser - Blocked APIs (By Design)

> **Security First**: These APIs are intentionally blocked to protect users from hardware-level attacks, fingerprinting, and privacy violations.

## ❌ Hardware Access APIs

| API | Risk | Status |
|-----|------|--------|
| **WebUSB** | Kernel-adjacent, device takeover | 🚫 BLOCKED |
| **WebBluetooth** | BLE device attacks, tracking | 🚫 BLOCKED |
| **WebSerial** | Serial port access | 🚫 BLOCKED |
| **WebHID** | Raw HID device access | 🚫 BLOCKED |
| **WebNFC** | NFC tag manipulation | 🚫 BLOCKED |
| **Local Font Access** | Fingerprinting vector | 🚫 BLOCKED |
| **File System Access** | Filesystem escape | 🚫 BLOCKED |

## ❌ Tracking / Ad-Tech APIs

| API | Risk | Status |
|-----|------|--------|
| **Topics API** | Interest-based tracking | 🚫 BLOCKED |
| **Attribution Reporting** | Cross-site tracking | 🚫 BLOCKED |
| **Fenced Frames** | Tracking containers | 🚫 BLOCKED |
| **Shared Storage** | Cross-origin state | 🚫 BLOCKED |

## ⏳ Not Implemented (Too Complex / Low ROI)

| API | Reason | Status |
|-----|--------|--------|
| **WebRTC** | Massive attack surface | ⏳ Future |
| **WebXR** | Niche use case | ⏳ Future |
| **WebTransport** | Experimental | ⏳ Future |

---

## Implementation

These APIs return `undefined` or throw `SecurityError` when accessed:

```javascript
navigator.usb       // undefined
navigator.bluetooth // undefined  
navigator.serial    // undefined
navigator.hid       // undefined
```

## Rationale

- **Safari** blocks most of these APIs → still a browser
- **Firefox** limits aggressively → still a browser  
- **Chrome** exposes everything → largest attack surface

Zepra prioritizes **security over feature parity**.
