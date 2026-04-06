// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// Start page HTML template for zepra://start

#pragma once

#include <string>

namespace ZepraBrowser {
namespace UI {

inline std::string getStartPageHTML() {
    return R"(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>New Tab - Zepra</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
  background: linear-gradient(135deg, #DEBCEE, #EBC8FA, #DFBDF0);
  font-family: sans-serif;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  color: #000;
}
.center {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 24px;
}
.logo-container {
  display: flex;
  align-items: center;
  justify-content: center;
}
.logo-text {
  font-size: 0;
}
.search-pill {
  display: flex;
  align-items: center;
  background: #fff;
  border-radius: 28px;
  padding: 8px 16px;
  width: 560px;
  min-height: 48px;
}
.pill-left {
  display: flex;
  align-items: center;
}
.pill-right {
  display: flex;
  align-items: center;
  justify-content: flex-end;
}
.action-btn {
  width: 24px;
  height: 24px;
  border-radius: 12px;
  background: #F0F0F0;
  color: #555;
  font-size: 11px;
  font-weight: bold;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-right: 8px;
}
.search-input {
  width: 480px;
  border: none;
  outline: none;
  background: transparent;
  font-size: 14px;
  font-weight: 700;
  color: #000;
  text-align: center;
  padding: 0;
}
.search-input::placeholder {
  color: #000;
  font-weight: 700;
}
.waveform { font-size: 16px; font-weight: bold; margin-right: 12px; color: #555; }
.arrow-up {
  width: 28px;
  height: 28px;
  background: #000;
  color: #fff;
  border-radius: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  font-weight: bold;
}
</style>
</head>
<body>
<div class="center">
  <div class="logo-container">
    <div class="logo-text"><img src="file://resources/icons/zepra.svg" /></div>
  </div>
  <div class="search-pill">
    <div class="pill-left">
      <div class="action-btn">+</div>
      <div class="action-btn">T</div>
    </div>
    <input class="search-input" type="text" placeholder="Let's give your dream life. What you create today?" autofocus />
    <div class="pill-right">
      <span class="waveform">|||</span>
      <div class="arrow-up">^</div>
    </div>
  </div>
</div>
</body>
</html>
)";
}

} // namespace UI
} // namespace ZepraBrowser
