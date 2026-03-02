//! Glassmorphism Effect
//!
//! Modern frosted glass UI effect combining blur, tint, border glow, and subtle noise.
//! Used for NeolyxOS desktop environment UI elements.

use crate::primitives::{Color, Rect};
use crate::effects::blur::{Blur, BlurQuality};

/// Glassmorphism style configuration
#[derive(Debug, Clone)]
pub struct Glassmorphism {
    /// Background blur radius (10-30px typical)
    pub blur_radius: f32,
    /// Tint/overlay color (usually semi-transparent white or dark)
    pub tint: Color,
    /// Saturation adjustment (0.0-2.0, 1.0 = normal)
    pub saturation: f32,
    /// Border glow intensity (0.0-1.0)
    pub border_glow: f32,
    /// Border glow color
    pub border_color: Color,
    /// Noise texture strength for glass texture (0.0-0.1)
    pub noise_strength: f32,
    /// Corner radius for rounded glass
    pub corner_radius: f32,
}

impl Default for Glassmorphism {
    fn default() -> Self {
        Self {
            blur_radius: 20.0,
            tint: Color::rgba(255, 255, 255, 40),  // Subtle white tint
            saturation: 1.2,
            border_glow: 0.3,
            border_color: Color::rgba(255, 255, 255, 60),
            noise_strength: 0.02,
            corner_radius: 16.0,
        }
    }
}

impl Glassmorphism {
    /// Create a new glassmorphism effect with default settings
    pub fn new() -> Self {
        Self::default()
    }
    
    /// Light glass style (for light backgrounds)
    pub fn light() -> Self {
        Self {
            tint: Color::rgba(255, 255, 255, 80),
            border_color: Color::rgba(255, 255, 255, 100),
            saturation: 1.1,
            ..Default::default()
        }
    }
    
    /// Dark glass style (for dark mode)
    pub fn dark() -> Self {
        Self {
            tint: Color::rgba(0, 0, 0, 60),
            border_color: Color::rgba(255, 255, 255, 30),
            saturation: 1.3,
            blur_radius: 25.0,
            ..Default::default()
        }
    }
    
    /// Acrylic style (more opaque, Windows 11-like)
    pub fn acrylic() -> Self {
        Self {
            blur_radius: 30.0,
            tint: Color::rgba(20, 20, 30, 180),
            saturation: 0.8,
            border_glow: 0.1,
            border_color: Color::rgba(100, 100, 150, 60),
            noise_strength: 0.04,
            corner_radius: 8.0,
        }
    }
    
    /// Mica style (subtle, background-based)
    pub fn mica() -> Self {
        Self {
            blur_radius: 60.0,
            tint: Color::rgba(200, 200, 220, 60),
            saturation: 0.9,
            border_glow: 0.0,
            border_color: Color::TRANSPARENT,
            noise_strength: 0.0,
            corner_radius: 8.0,
        }
    }
    
    /// Set blur radius
    pub fn blur(mut self, radius: f32) -> Self {
        self.blur_radius = radius;
        self
    }
    
    /// Set tint color
    pub fn tint_color(mut self, color: Color) -> Self {
        self.tint = color;
        self
    }
    
    /// Set border glow
    pub fn glow(mut self, intensity: f32, color: Color) -> Self {
        self.border_glow = intensity;
        self.border_color = color;
        self
    }
    
    /// Set noise strength
    pub fn noise(mut self, strength: f32) -> Self {
        self.noise_strength = strength;
        self
    }
    
    /// Set corner radius
    pub fn rounded(mut self, radius: f32) -> Self {
        self.corner_radius = radius;
        self
    }
    
    /// Get the blur configuration for this glass effect
    pub fn blur_config(&self) -> Blur {
        Blur::new(self.blur_radius).quality(BlurQuality::High)
    }
    
    /// Calculate the expanded bounds needed for blur and glow
    pub fn expanded_bounds(&self, rect: Rect) -> Rect {
        let expand = self.blur_radius.max(self.border_glow * 10.0).ceil();
        Rect::new(
            rect.x - expand,
            rect.y - expand,
            rect.width + expand * 2.0,
            rect.height + expand * 2.0,
        )
    }
    
    /// Apply saturation adjustment to a color
    pub fn apply_saturation(&self, color: Color) -> Color {
        let r = color.r as f32 / 255.0;
        let g = color.g as f32 / 255.0;
        let b = color.b as f32 / 255.0;
        
        // Convert to grayscale using luminance
        let gray = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        
        // Mix between gray and color based on saturation
        let new_r = gray + (r - gray) * self.saturation;
        let new_g = gray + (g - gray) * self.saturation;
        let new_b = gray + (b - gray) * self.saturation;
        
        Color::rgba(
            (new_r.clamp(0.0, 1.0) * 255.0) as u8,
            (new_g.clamp(0.0, 1.0) * 255.0) as u8,
            (new_b.clamp(0.0, 1.0) * 255.0) as u8,
            color.a,
        )
    }
    
    /// Generate noise pattern for glass texture (returns alpha values 0-255)
    pub fn generate_noise(&self, width: u32, height: u32, seed: u32) -> Vec<u8> {
        let mut noise = Vec::with_capacity((width * height) as usize);
        let strength_u8 = (self.noise_strength * 255.0) as u8;
        
        // Simple pseudo-random noise
        let mut state = seed;
        for _ in 0..(width * height) {
            // LCG random
            state = state.wrapping_mul(1103515245).wrapping_add(12345);
            let rand = ((state >> 16) & 0xFF) as u8;
            
            // Scale by noise strength
            let noise_val = ((rand as u16 * strength_u8 as u16) / 255) as u8;
            noise.push(noise_val);
        }
        
        noise
    }
}

/// Apply glassmorphism effect to pixel buffer (CPU implementation)
pub fn apply_glassmorphism(
    background: &[u8],      // Background pixels to blur (RGBA)
    output: &mut [u8],      // Output buffer (RGBA)
    width: u32,
    height: u32,
    style: &Glassmorphism,
) {
    use crate::effects::blur::box_blur_rgba_multi;
    
    // Step 1: Copy background and apply blur
    output.copy_from_slice(background);
    let blur_radius = (style.blur_radius / 3.0).ceil() as u32;
    box_blur_rgba_multi(output, width, height, blur_radius, 3);
    
    // Step 2: Apply saturation adjustment and tint
    for i in (0..output.len()).step_by(4) {
        let color = Color::rgba(output[i], output[i + 1], output[i + 2], output[i + 3]);
        let saturated = style.apply_saturation(color);
        
        // Blend with tint
        let tint_alpha = style.tint.a as f32 / 255.0;
        output[i] = ((saturated.r as f32 * (1.0 - tint_alpha) + style.tint.r as f32 * tint_alpha) as u8);
        output[i + 1] = ((saturated.g as f32 * (1.0 - tint_alpha) + style.tint.g as f32 * tint_alpha) as u8);
        output[i + 2] = ((saturated.b as f32 * (1.0 - tint_alpha) + style.tint.b as f32 * tint_alpha) as u8);
        // Keep alpha from saturated
        output[i + 3] = saturated.a;
    }
    
    // Step 3: Add noise if enabled
    if style.noise_strength > 0.0 {
        let noise = style.generate_noise(width, height, 12345);
        for (i, &n) in noise.iter().enumerate() {
            let idx = i * 4;
            // Add noise to each channel
            output[idx] = output[idx].saturating_add(n / 3);
            output[idx + 1] = output[idx + 1].saturating_add(n / 3);
            output[idx + 2] = output[idx + 2].saturating_add(n / 3);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_glassmorphism_presets() {
        let light = Glassmorphism::light();
        assert!(light.tint.a > 60);
        
        let dark = Glassmorphism::dark();
        assert!(dark.saturation > 1.0);
        
        let acrylic = Glassmorphism::acrylic();
        assert!(acrylic.noise_strength > 0.0);
    }
    
    #[test]
    fn test_saturation() {
        let glass = Glassmorphism::new();
        let red = Color::rgb(255, 0, 0);
        let saturated = glass.apply_saturation(red);
        
        // Red should still be mostly red
        assert!(saturated.r > 200);
    }
    
    #[test]
    fn test_noise_generation() {
        let glass = Glassmorphism::new().noise(0.1);
        let noise = glass.generate_noise(10, 10, 42);
        
        assert_eq!(noise.len(), 100);
        // Should have some variation
        assert!(noise.iter().any(|&n| n > 0));
    }
}
