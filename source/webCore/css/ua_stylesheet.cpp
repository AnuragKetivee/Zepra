// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
/**
 * ua_stylesheet.cpp - User-Agent Stylesheet for ZepraBrowser
 *
 * Defines browser default styles for all HTML elements.
 * Based on the CSS2/CSS3 specifications and WebKit's html.css.
 *
 * TODO(NeolyxOS): Load UA stylesheet from disk when running on NeolyxOS
 * to allow system-level theming. Replace the embedded string with a
 * file read from /sys/theme/zepra.css or equivalent NeolyxOS path.
 */

#include "ua_stylesheet.h"

namespace Zepra::WebCore {

const char* ZepraUAStylesheet::getStylesheet() {
    // TODO(NeolyxOS): Replace with disk-load from /sys/theme/zepra.css
    static const char* UA_STYLESHEET = R"css(

/* ==========================================================================
   Root & Document
   ========================================================================== */

html {
    display: block;
    color: #1f2328;
}

/* ==========================================================================
   Hidden Elements — display: none for non-rendering elements
   ========================================================================== */

head, link, meta, script, style, title,
area, base, basefont, datalist, noembed,
noframes, param, rp, template {
    display: none;
}

[hidden] {
    display: none !important;
}

/* ==========================================================================
   Block-Level Elements
   ========================================================================== */

body {
    display: block;
    margin: 8px;
    font-family: sans-serif;
    font-size: 16px;
}

address, article, aside, div, footer,
header, hgroup, main, nav, search, section {
    display: block;
}

p {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
}

blockquote {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 40px;
    margin-right: 40px;
}

figure {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 40px;
    margin-right: 40px;
}

figcaption {
    display: block;
}

hr {
    display: block;
    overflow: hidden;
    margin-top: 8px;
    margin-bottom: 8px;
    border-top: 1px solid #ccc;
}

pre {
    display: block;
    font-family: monospace;
    white-space: pre;
    margin-top: 1em;
    margin-bottom: 1em;
}

/* ==========================================================================
   Headings
   ========================================================================== */

h1 {
    display: block;
    font-size: 32px;
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 10px;
    color: #1a1a1a;
}

h2 {
    display: block;
    font-size: 24px;
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 10px;
    color: #1a1a1a;
}

h3 {
    display: block;
    font-size: 20px;
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 10px;
    color: #1a1a1a;
}

h4 {
    display: block;
    font-size: 16px;
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 10px;
}

h5 {
    display: block;
    font-size: 13px;
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 10px;
}

h6 {
    display: block;
    font-size: 11px;
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 10px;
}

/* ==========================================================================
   Lists
   ========================================================================== */

ul, menu, dir {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    padding-left: 40px;
}

ol {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    padding-left: 40px;
}

li {
    display: list-item;
    margin-top: 4px;
    margin-bottom: 4px;
}

dl {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
}

dd {
    display: block;
    margin-left: 40px;
}

dt {
    display: block;
}

/* ==========================================================================
   Inline Elements
   ========================================================================== */

span, a, strong, b, em, i, small, code,
label, abbr, cite, dfn, kbd, mark, q,
s, samp, sub, sup, u, var, wbr, time {
    display: inline;
}

a {
    color: #0066cc;
    text-decoration: underline;
}

strong, b {
    font-weight: bold;
}

em, i {
    font-style: italic;
}

small {
    font-size: 13px;
}

code, kbd, samp {
    font-family: monospace;
}

/* ==========================================================================
   Tables
   ========================================================================== */

table {
    display: block;
    border-collapse: separate;
    border-spacing: 2px;
}

thead { display: block; }
tbody { display: block; }
tfoot { display: block; }
tr    { display: block; }

td, th {
    display: inline;
    padding: 1px;
}

th {
    font-weight: bold;
    text-align: center;
}

caption {
    display: block;
    text-align: center;
}

/* ==========================================================================
   Forms
   ========================================================================== */

form {
    display: block;
}

input, textarea, select, button {
    display: inline-block;
    font-size: 14px;
    color: #333;
}

input {
    border: 1px solid #ccc;
    padding: 4px 8px;
    background-color: #fff;
}

textarea {
    border: 1px solid #ccc;
    padding: 4px;
    background-color: #fff;
}

button {
    padding: 4px 12px;
    border: 1px solid #ccc;
    background-color: #f0f0f0;
    cursor: pointer;
}

fieldset {
    display: block;
    margin-left: 2px;
    margin-right: 2px;
    padding-top: 6px;
    padding-bottom: 10px;
    padding-left: 12px;
    padding-right: 12px;
    border: 2px solid #ccc;
}

legend {
    padding-left: 2px;
    padding-right: 2px;
}

/* ==========================================================================
   Media
   ========================================================================== */

img {
    display: inline;
}

video, audio {
    display: inline-block;
}

/* ==========================================================================
   Semantic/Interactive
   ========================================================================== */

details {
    display: block;
}

summary {
    display: block;
    cursor: pointer;
}

dialog {
    display: none;
    position: absolute;
    border: 1px solid #333;
    padding: 1em;
    background-color: #fff;
}

)css";
    return UA_STYLESHEET;
}

} // namespace Zepra::WebCore
