// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// Error/fallback page generators and content type handlers

#pragma once

#include <string>
#include <functional>

namespace ZepraBrowser {
namespace UI {

// Calculates layout coordinates for the "Nothing Found" page
// Caller provides gfx calls through their own rendering layer
struct NothingFoundLayout {
    float iconX, iconY, iconW, iconH;
    float searchIconX, searchIconY, searchIconSize;
    float titleX, titleY;
    float sub1X, sub1Y;
    float sub2X, sub2Y;
    float btnX, btnY, btnW, btnH;
    float btnTextX, btnTextY;

    static NothingFoundLayout compute(float contentX, float contentY, float contentW, float contentH) {
        NothingFoundLayout l;
        float cx = contentX + contentW / 2;
        float cy = contentY + contentH / 2 - 60;
        l.iconX = cx - 40;  l.iconY = cy - 60;  l.iconW = 80;  l.iconH = 80;
        l.searchIconX = cx - 24; l.searchIconY = cy - 44; l.searchIconSize = 48;
        l.titleX = cx - 70; l.titleY = cy + 50;
        l.sub1X = cx - 130; l.sub1Y = cy + 80;
        l.sub2X = cx - 150; l.sub2Y = cy + 100;
        l.btnX = cx - 100; l.btnY = cy + 130; l.btnW = 200; l.btnH = 40;
        l.btnTextX = cx - 75; l.btnTextY = cy + 156;
        return l;
    }
};

// Recursively checks if a layout tree has any visible content
template<typename BoxType>
bool hasVisibleContent(const BoxType& box) {
    if (!box.text.empty() || box.isImage || box.isInput) return true;
    for (const auto& child : box.children) {
        if (hasVisibleContent(child)) return true;
    }
    return false;
}

// PDF viewer HTML
inline std::string getPdfViewerHTML(const std::string& title, int pageCount,
    std::function<std::string(int)> extractText)
{
    std::string html = "<html><head><title>" + title + "</title>";
    html += "<style>";
    html += "body { font-family: sans-serif; background: #f5f5f5; color: #333; padding: 20px; margin: 0; }";
    html += ".pdf-header { background: #2d2d2d; color: #fff; padding: 16px 24px; border-radius: 8px; margin-bottom: 20px; }";
    html += ".pdf-header h1 { margin: 0; font-size: 20px; }";
    html += ".pdf-header p { margin: 4px 0 0; font-size: 13px; color: #aaa; }";
    html += ".pdf-page { background: #fff; padding: 24px 32px; margin-bottom: 16px; border-radius: 6px; border: 1px solid #ddd; }";
    html += ".pdf-page h2 { font-size: 14px; color: #888; border-bottom: 1px solid #eee; padding-bottom: 8px; margin-top: 0; }";
    html += ".pdf-page pre { white-space: pre-wrap; word-wrap: break-word; font-family: sans-serif; font-size: 14px; line-height: 1.6; }";
    html += "</style></head><body>";

    html += "<div class='pdf-header'><h1>" + title + "</h1>";
    html += "<p>" + std::to_string(pageCount) + " pages</p></div>";

    for (int i = 0; i < pageCount; i++) {
        std::string pageText = extractText(i);
        size_t start = pageText.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            pageText = pageText.substr(start);
            size_t end = pageText.find_last_not_of(" \t\n\r");
            if (end != std::string::npos) pageText = pageText.substr(0, end + 1);
        } else {
            pageText.clear();
        }

        html += "<div class='pdf-page'><h2>Page " + std::to_string(i + 1) + "</h2>";
        if (!pageText.empty())
            html += "<pre>" + pageText + "</pre>";
        else
            html += "<p style='color:#999;font-style:italic;'>Text extraction not available for this page (embedded fonts)</p>";
        html += "</div>";
    }

    html += "</body></html>";
    return html;
}

// Image viewer HTML wrapper
inline std::string getImageViewerHTML(const std::string& title, const std::string& imageUrl) {
    return "<html><head><title>" + title + "</title></head>"
        "<body style='margin:0;display:flex;justify-content:center;align-items:center;min-height:100vh;background:#1a1a1a;'>"
        "<img src='" + imageUrl + "' style='max-width:90vw;max-height:90vh;object-fit:contain;' /></body></html>";
}

} // namespace UI
} // namespace ZepraBrowser
