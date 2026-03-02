/**
 * @file webapp_manifest.h
 * @brief Web App Manifest Parser for NeolyxOS
 * 
 * Parses W3C Web App Manifests (manifest.json) to create
 * native NeolyxOS app bundles from Progressive Web Apps.
 * 
 * ZepraBrowser uses this to install web apps like:
 * - Lyx Store (com.ketivee.lyxstore)
 * - Music (com.ketivee.music)
 * - Calendar (com.ketivee.calendar)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ZEPRA_WEBAPP_MANIFEST_H
#define ZEPRA_WEBAPP_MANIFEST_H

#include <string>
#include <vector>

namespace Zepra::WebApp {

/**
 * Display mode for web app
 */
enum class DisplayMode {
    Browser,      // Normal browser tab
    Standalone,   // Standalone window (no browser UI)
    Fullscreen,   // Fullscreen app
    MinimalUI     // Minimal browser controls
};

/**
 * Icon definition from manifest
 */
struct ManifestIcon {
    std::string src;
    std::string sizes;  // "256x256", "512x512"
    std::string type;   // "image/png"
    std::string purpose; // "any", "maskable"
};

/**
 * Shortcut definition (app shortcuts menu)
 */
struct ManifestShortcut {
    std::string name;
    std::string short_name;
    std::string url;
    std::string description;
    std::vector<ManifestIcon> icons;
};

/**
 * Parsed Web App Manifest
 */
struct WebAppManifest {
    /* Required fields */
    std::string name;           // "Lyx Store"
    std::string short_name;     // "Store"
    std::string start_url;      // "/store" or "https://..."
    
    /* Optional display */
    DisplayMode display = DisplayMode::Standalone;
    std::string background_color;   // "#1a1a2e"
    std::string theme_color;        // "#6c5ce7"
    std::string orientation;        // "portrait", "landscape", "any"
    
    /* Icons */
    std::vector<ManifestIcon> icons;
    
    /* Optional features */
    std::string description;
    std::string scope;
    std::string id;
    std::vector<std::string> categories;
    std::vector<ManifestShortcut> shortcuts;
    
    /* NeolyxOS extensions */
    std::string nx_app_id;      // "com.ketivee.lyxstore"
    bool nx_offline_capable = false;
    std::vector<std::string> nx_capabilities;
};

/**
 * Parse manifest.json content
 * @param json_content   Raw JSON string
 * @param base_url       Base URL for resolving relative paths
 * @param out            Output manifest structure
 * @return true on success
 */
bool parseManifest(const std::string& json_content, 
                   const std::string& base_url,
                   WebAppManifest& out);

/**
 * Fetch and parse manifest from URL
 * @param manifest_url   URL to manifest.json
 * @param out            Output manifest structure
 * @return true on success
 */
bool fetchManifest(const std::string& manifest_url, WebAppManifest& out);

/**
 * Generate NeolyxOS app ID from manifest
 * @param manifest   Parsed manifest
 * @return App ID like "com.example.appname"
 */
std::string generateAppId(const WebAppManifest& manifest);

/**
 * Get best icon for given size
 * @param manifest   Parsed manifest
 * @param size       Desired size (256, 512)
 * @return Icon URL or empty if none
 */
std::string getBestIcon(const WebAppManifest& manifest, int size);

/**
 * Convert display string to enum
 */
DisplayMode parseDisplayMode(const std::string& str);

} // namespace Zepra::WebApp

#endif // ZEPRA_WEBAPP_MANIFEST_H
