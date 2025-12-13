# Zepra Browser - API Support

## ✅ TIER 1: Core APIs (Fully Supported)

### DOM & Runtime
- Document Object Model (DOM)
- HTML DOM API  
- Window, Document, Element, Node
- Event / UI Events
- History API
- Navigator API ✅
- Selection API

### Networking
- Fetch API ✅
- XMLHttpRequest
- URL / URLSearchParams
- WebSocket API
- Server-Sent Events

### Storage
- localStorage / sessionStorage ✅
- IndexedDB (planned)
- Cache API (planned)

### Security
- Same-Origin Policy
- Permissions API (planned)
- Web Crypto API (planned)
- CSP (basic)

---

## ✅ TIER 2: Modern APIs (In Progress)

### Rendering
- Canvas API (planned)
- SVG API
- Web Animations API
- Resize/Intersection/Mutation Observers
- requestAnimationFrame

### Media
- Web Audio API (planned - NXAudio backend)
- WebGL 2.0 (stub)
- WebCodecs (planned)
- Picture-in-Picture

### Workers
- Web Workers (planned)
- Service Workers (planned)
- Streams API

---

## 🟡 TIER 3: Controlled APIs (Permission-Gated)

- Vibration API ✅ (requires user gesture)
- Gamepad API (planned)
- Fullscreen API
- Screen Orientation
- Notifications API (requires permission)
- Geolocation (coarse only) ✅

---

## ❌ TIER 4: Blocked by Design

See [API_BLOCKED_BY_DESIGN.md](./API_BLOCKED_BY_DESIGN.md)
