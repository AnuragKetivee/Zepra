/**
 * @file webapp_installer.h
 * @brief Web App Installer for NeolyxOS
 * 
 * Installs Progressive Web Apps as native NeolyxOS applications.
 * Creates proper app bundle with Info.nxpt, Resources, and launcher.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ZEPRA_WEBAPP_INSTALLER_H
#define ZEPRA_WEBAPP_INSTALLER_H

#include "webapp_manifest.h"
#include <string>
#include <functional>

namespace Zepra::WebApp {

/**
 * Installation result status
 */
enum class InstallResult {
    Success,
    ManifestError,
    DownloadError,
    FileSystemError,
    AlreadyInstalled,
    UserCanceled
};

/**
 * Installation progress callback
 */
using ProgressCallback = std::function<void(int percent, const std::string& status)>;

/**
 * Web App Installer
 * 
 * Installs PWAs to ~/Applications/ with proper NeolyxOS structure:
 * 
 * ~/Applications/AppName/
 * ├── AppName              # Launcher script
 * ├── Info.nxpt            # App metadata
 * └── Resources/
 *     └── icon.png         # Downloaded icon
 */
class WebAppInstaller {
public:
    WebAppInstaller();
    ~WebAppInstaller();
    
    /**
     * Install web app from manifest
     * @param manifest   Parsed web app manifest
     * @param callback   Progress callback (optional)
     * @return Installation result
     */
    InstallResult install(const WebAppManifest& manifest, 
                          ProgressCallback callback = nullptr);
    
    /**
     * Install web app from URL (fetches manifest automatically)
     * @param url        URL of the web app
     * @param callback   Progress callback
     * @return Installation result
     */
    InstallResult installFromUrl(const std::string& url,
                                 ProgressCallback callback = nullptr);
    
    /**
     * Uninstall a web app
     * @param app_id     App bundle ID (com.example.app)
     * @return true if uninstalled successfully
     */
    bool uninstall(const std::string& app_id);
    
    /**
     * Check if web app is already installed
     * @param app_id     App bundle ID
     * @return true if installed
     */
    bool isInstalled(const std::string& app_id) const;
    
    /**
     * Get installation path for an app
     * @param app_name   App name
     * @return Full path (~/Applications/AppName/)
     */
    std::string getInstallPath(const std::string& app_name) const;
    
    /**
     * Get last error message
     */
    const std::string& lastError() const { return last_error_; }
    
private:
    std::string install_base_;  // ~/Applications/
    std::string last_error_;
    
    /**
     * Create Info.nxpt for web app
     */
    bool createInfoNxpt(const std::string& path, const WebAppManifest& manifest);
    
    /**
     * Download and save app icon
     */
    bool downloadIcon(const std::string& icon_url, const std::string& dest_path);
    
    /**
     * Create launcher script
     */
    bool createLauncher(const std::string& path, const WebAppManifest& manifest);
    
    /**
     * Resolve user home directory
     */
    std::string expandPath(const std::string& path) const;
};

/**
 * Quick install helper
 * @param url   URL of web app to install
 * @return true on success
 */
bool quickInstall(const std::string& url);

/**
 * List installed web apps
 * @return Vector of app IDs
 */
std::vector<std::string> listInstalledWebApps();

} // namespace Zepra::WebApp

#endif // ZEPRA_WEBAPP_INSTALLER_H
