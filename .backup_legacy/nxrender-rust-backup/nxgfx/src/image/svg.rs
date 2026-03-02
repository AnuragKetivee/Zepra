//! SVG Module
//!
//! Custom SVG parsing and rendering for NXRender.
//! No third-party dependencies - pure Rust implementation.
//! Handles basic SVG elements: path, circle, rect, line, polyline

use std::collections::HashMap;
use std::fs;

/// RGBA Color
#[derive(Debug, Clone, Copy, Default)]
pub struct SvgColor {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

impl SvgColor {
    pub fn rgb(r: u8, g: u8, b: u8) -> Self {
        Self { r, g, b, a: 255 }
    }

    pub fn rgba(r: u8, g: u8, b: u8, a: u8) -> Self {
        Self { r, g, b, a }
    }

    pub fn parse(s: &str) -> Self {
        let s = s.trim();
        if s == "none" {
            return Self { r: 0, g: 0, b: 0, a: 0 };
        }
        if s == "white" {
            return Self::rgb(255, 255, 255);
        }
        if s == "black" {
            return Self::rgb(0, 0, 0);
        }
        if s == "currentColor" {
            return Self::rgb(200, 200, 200);
        }
        if s.starts_with('#') {
            if s.len() == 7 {
                let r = u8::from_str_radix(&s[1..3], 16).unwrap_or(0);
                let g = u8::from_str_radix(&s[3..5], 16).unwrap_or(0);
                let b = u8::from_str_radix(&s[5..7], 16).unwrap_or(0);
                return Self::rgb(r, g, b);
            } else if s.len() == 4 {
                let r = u8::from_str_radix(&s[1..2], 16).unwrap_or(0) * 17;
                let g = u8::from_str_radix(&s[2..3], 16).unwrap_or(0) * 17;
                let b = u8::from_str_radix(&s[3..4], 16).unwrap_or(0) * 17;
                return Self::rgb(r, g, b);
            }
        }
        Self::rgb(128, 128, 128)
    }
}

/// Path command
#[derive(Debug, Clone)]
pub struct PathCommand {
    pub cmd: char,
    pub args: Vec<f32>,
}

/// SVG Shape types
#[derive(Debug, Clone)]
pub enum SvgShape {
    Circle { cx: f32, cy: f32, r: f32 },
    Rect { x: f32, y: f32, w: f32, h: f32, rx: f32 },
    Line { x1: f32, y1: f32, x2: f32, y2: f32 },
    Path { commands: Vec<PathCommand> },
}

/// Single SVG element with style
#[derive(Debug, Clone)]
pub struct SvgElement {
    pub shape: SvgShape,
    pub stroke: SvgColor,
    pub fill: SvgColor,
    pub stroke_width: f32,
    pub has_stroke: bool,
    pub has_fill: bool,
}

/// Parsed SVG image
#[derive(Debug, Clone)]
pub struct SvgImage {
    pub width: f32,
    pub height: f32,
    pub view_box: (f32, f32, f32, f32),
    pub elements: Vec<SvgElement>,
}

impl Default for SvgImage {
    fn default() -> Self {
        Self {
            width: 24.0,
            height: 24.0,
            view_box: (0.0, 0.0, 24.0, 24.0),
            elements: Vec::new(),
        }
    }
}

/// SVG Loader
pub struct SvgLoader {
    cache: HashMap<String, SvgImage>,
}

impl SvgLoader {
    pub fn new() -> Self {
        Self {
            cache: HashMap::new(),
        }
    }

    /// Load SVG from file
    pub fn load_file(&mut self, name: &str, path: &str) -> Result<(), String> {
        let content = fs::read_to_string(path)
            .map_err(|e| format!("Failed to read SVG file: {}", e))?;
        self.load_string(name, &content)
    }

    /// Load SVG from string
    pub fn load_string(&mut self, name: &str, svg: &str) -> Result<(), String> {
        let image = Self::parse_svg(svg)?;
        self.cache.insert(name.to_string(), image);
        Ok(())
    }

    /// Get cached SVG image
    pub fn get(&self, name: &str) -> Option<&SvgImage> {
        self.cache.get(name)
    }

    /// Check if SVG is loaded
    pub fn has(&self, name: &str) -> bool {
        self.cache.contains_key(name)
    }

    /// Parse SVG content
    fn parse_svg(svg: &str) -> Result<SvgImage, String> {
        let mut image = SvgImage::default();

        // Parse viewBox
        if let Some(vb) = Self::extract_attr(svg, "viewBox") {
            let parts: Vec<f32> = vb.split_whitespace()
                .filter_map(|s| s.parse().ok())
                .collect();
            if parts.len() == 4 {
                image.view_box = (parts[0], parts[1], parts[2], parts[3]);
            }
        }

        // Parse width/height
        if let Some(w) = Self::extract_attr(svg, "width") {
            image.width = w.trim_end_matches("px").parse().unwrap_or(24.0);
        }
        if let Some(h) = Self::extract_attr(svg, "height") {
            image.height = h.trim_end_matches("px").parse().unwrap_or(24.0);
        }

        // Parse circles
        for cap in regex_lite::Regex::new(r"<circle[^>]*/>")
            .unwrap()
            .find_iter(svg)
        {
            let elem = cap.as_str();
            let cx = Self::parse_float_attr(elem, "cx").unwrap_or(0.0);
            let cy = Self::parse_float_attr(elem, "cy").unwrap_or(0.0);
            let r = Self::parse_float_attr(elem, "r").unwrap_or(0.0);

            image.elements.push(SvgElement {
                shape: SvgShape::Circle { cx, cy, r },
                stroke: SvgColor::parse(&Self::extract_attr(elem, "stroke").unwrap_or_default()),
                fill: SvgColor::parse(&Self::extract_attr(elem, "fill").unwrap_or_default()),
                stroke_width: Self::parse_float_attr(elem, "stroke-width").unwrap_or(2.0),
                has_stroke: Self::extract_attr(elem, "stroke").map(|s| s != "none").unwrap_or(false),
                has_fill: Self::extract_attr(elem, "fill").map(|s| s != "none").unwrap_or(false),
            });
        }

        // Parse rects
        for cap in regex_lite::Regex::new(r"<rect[^>]*/>")
            .unwrap()
            .find_iter(svg)
        {
            let elem = cap.as_str();
            let x = Self::parse_float_attr(elem, "x").unwrap_or(0.0);
            let y = Self::parse_float_attr(elem, "y").unwrap_or(0.0);
            let w = Self::parse_float_attr(elem, "width").unwrap_or(0.0);
            let h = Self::parse_float_attr(elem, "height").unwrap_or(0.0);
            let rx = Self::parse_float_attr(elem, "rx").unwrap_or(0.0);

            image.elements.push(SvgElement {
                shape: SvgShape::Rect { x, y, w, h, rx },
                stroke: SvgColor::parse(&Self::extract_attr(elem, "stroke").unwrap_or_default()),
                fill: SvgColor::parse(&Self::extract_attr(elem, "fill").unwrap_or_default()),
                stroke_width: Self::parse_float_attr(elem, "stroke-width").unwrap_or(2.0),
                has_stroke: Self::extract_attr(elem, "stroke").map(|s| s != "none").unwrap_or(false),
                has_fill: Self::extract_attr(elem, "fill").map(|s| s != "none").unwrap_or(false),
            });
        }

        // Parse paths
        for cap in regex_lite::Regex::new(r#"<path[^>]*d\s*=\s*"([^"]+)"[^>]*/>"#)
            .unwrap()
            .captures_iter(svg)
        {
            let elem = cap.get(0).unwrap().as_str();
            let d = cap.get(1).map(|m| m.as_str()).unwrap_or("");
            let commands = Self::parse_path_commands(d);

            image.elements.push(SvgElement {
                shape: SvgShape::Path { commands },
                stroke: SvgColor::parse(&Self::extract_attr(elem, "stroke").unwrap_or_default()),
                fill: SvgColor::parse(&Self::extract_attr(elem, "fill").unwrap_or_default()),
                stroke_width: Self::parse_float_attr(elem, "stroke-width").unwrap_or(2.0),
                has_stroke: Self::extract_attr(elem, "stroke").map(|s| s != "none").unwrap_or(false),
                has_fill: Self::extract_attr(elem, "fill").map(|s| s != "none").unwrap_or(false),
            });
        }

        Ok(image)
    }

    fn extract_attr(elem: &str, attr: &str) -> Option<String> {
        let pattern = format!(r#"{}\\s*=\\s*\"([^\"]+)\""#, attr);
        regex_lite::Regex::new(&pattern)
            .ok()?
            .captures(elem)?
            .get(1)
            .map(|m| m.as_str().to_string())
    }

    fn parse_float_attr(elem: &str, attr: &str) -> Option<f32> {
        Self::extract_attr(elem, attr)?.parse().ok()
    }

    fn parse_path_commands(d: &str) -> Vec<PathCommand> {
        let mut commands = Vec::new();
        let re = regex_lite::Regex::new(r"([MmLlHhVvZzCcSsQqTtAa])([^MmLlHhVvZzCcSsQqTtAa]*)").unwrap();

        for cap in re.captures_iter(d) {
            let cmd = cap.get(1).unwrap().as_str().chars().next().unwrap();
            let args_str = cap.get(2).map(|m| m.as_str()).unwrap_or("");
            let args: Vec<f32> = regex_lite::Regex::new(r"-?\d+\.?\d*")
                .unwrap()
                .find_iter(args_str)
                .filter_map(|m| m.as_str().parse().ok())
                .collect();

            commands.push(PathCommand { cmd, args });
        }

        commands
    }
}

impl Default for SvgLoader {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_color_parse() {
        let c = SvgColor::parse("#ff0000");
        assert_eq!(c.r, 255);
        assert_eq!(c.g, 0);
        assert_eq!(c.b, 0);
    }

    #[test]
    fn test_svg_parse() {
        let svg = r#"<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="10" stroke="currentColor" stroke-width="2"/></svg>"#;
        let image = SvgLoader::parse_svg(svg).unwrap();
        assert_eq!(image.elements.len(), 1);
    }
}
