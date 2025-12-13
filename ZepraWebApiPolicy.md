ZEPRA WEB API POLICY

What to ADD, what to PARTIALLY ADD, and what to IGNORE (by design)

I’ll also explicitly mark WHY.

✅ TIER 1 — MUST ADD (NON-NEGOTIABLE CORE)

If you skip these, your browser will be broken, unsafe, or incompatible.

Core DOM & Runtime

Document Object Model (DOM)

HTML DOM API

Window

Document

Element / Node / EventTarget

Event / UI Events

History API

Location

Navigator

Selection API

🔒 Reason: Fundamental execution + security boundaries.

Networking & Data

Fetch API

Headers / Request / Response

XMLHttpRequest (legacy, sadly required)

URL API

URLSearchParams

Server-Sent Events

WebSocket API

🔒 Reason: Almost every site depends on these.

Storage (Sandboxed)

Web Storage API (localStorage, sessionStorage)

IndexedDB API

Cache API

Storage API

Storage Access API

🔒 Reason: Offline apps, auth, state, performance.

Security & Integrity

Same-Origin Policy

Permissions API

Trusted Types API

Web Crypto API

CSP (basic)

Secure Context checks

🔒 Reason: Without these → unsafe browser.

✅ TIER 2 — MODERN EXPECTED (HIGH PRIORITY)

These are expected by modern sites, but can be incremental.

Rendering & UX

Canvas API

SVG API

Web Animations API

Resize Observer

Intersection Observer

Mutation Observer

requestAnimationFrame

Viewport API

VisualViewport

🎯 Reason: Layout engines, SPA frameworks, rendering logic.

Media (IMPORTANT for you)

Web Audio API

Media Capture and Streams

Media Source API

Media Capabilities API

Picture-in-Picture API

WebCodecs API ✅ (huge win for cloud gaming)

WebGL (2.0 minimum)

WebGPU API (controlled rollout)

🎮 Reason:
Cloud gaming, streaming, low-latency pipelines.
Your C/Rust media stack fits perfectly here.

Workers & Concurrency

Web Workers API

Shared Workers

Service Worker API

Streams API

MessageChannel / postMessage

⚙️ Reason: Performance, isolation, modern app models.

🟡 TIER 3 — CONDITIONAL / CONTROLLED (ADD WITH GUARDS)

These APIs are useful only if permission-gated and sandboxed.

Device & UX Enhancements

Vibration API ✅ (you already added — good for gaming)

Fullscreen API

Pointer Lock API

Gamepad API

Screen Orientation API

Screen Wake Lock API

Notifications API (strict permission model)

🎮 Reason:
Gaming, immersive UX, cloud streaming.

Sensors (Very Careful)

Geolocation API (coarse only by default)

Device Orientation (opt-in)

Battery Status API (read-only, rate-limited)

⚠️ Reason: Fingerprinting + privacy risks.

❌ TIER 4 — IGNORE OR BLOCK (BY DESIGN)

Skipping these makes your browser SAFER, not weaker.

Hardware / Device APIs (High Risk)

WebUSB ❌

WebBluetooth ❌

WebSerial ❌

WebHID ❌

WebNFC ❌

Local Font Access ❌

File System Access API ❌ (unless sandboxed virtual FS)

🔐 Reason: Massive attack surface, kernel-adjacent risks.

Experimental / Ad-Tech / Tracking

Topics API ❌

Attribution Reporting API ❌

Fenced Frames ❌

Shared Storage ❌

Speculation Rules ❌ (unless internal-only)

🛑 Reason: Privacy, tracking, regulatory risk.

Extremely Complex / Low ROI (for now)

WebRTC ❌ (huge code + attack surface)

WebXR ❌

WebVR ❌ (deprecated anyway)

WebTransport ❌ (future revisit)

⏳ Reason: Not required for “browser correctness”.

🔥 SPECIAL NOTE: Your VIBRATION decision

You did something smart here.

Using Vibration API + Notifications for cloud gaming haptics is a legit use case.

Just do this:

Require user gesture

Allow per-site toggle

Rate-limit patterns

Disable in background tabs

That’s console-grade UX, not a gimmick.

📌 What REAL browsers do (truth)

Safari blocks half of these APIs → still a browser

Firefox limits aggressively → still a browser

Chrome exposes everything → also the largest attack surface

So no — “supporting all MDN APIs” is NOT the definition of a browser.

✅ Final Answer (Crystal Clear)

❌ You do NOT need to add all MDN APIs

❌ Adding all would make Zepra less secure

✅ You MUST add a secure core

✅ You SHOULD add media + rendering APIs (your strength)

✅ You SHOULD explicitly block dangerous APIs

✅ That still makes Zepra a real, modern browser

📄 STRONG RECOMMENDATION

Create these files in your repo:

API_SUPPORT.md

API_BLOCKED_BY_DESIGN.md

SECURITY_MODEL.md

This is exactly how serious browser engines operate.