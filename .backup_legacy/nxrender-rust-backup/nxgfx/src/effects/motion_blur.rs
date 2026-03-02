//! Motion Blur Effect
//!
//! Velocity-based motion blur for smooth animations.
//! Creates a sense of speed and movement in UI transitions.

use crate::primitives::{Color, Rect, Point};

/// 2D Vector for velocity
#[derive(Debug, Clone, Copy, Default)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}

impl Vec2 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0 };
    
    pub fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }
    
    pub fn length(&self) -> f32 {
        (self.x * self.x + self.y * self.y).sqrt()
    }
    
    pub fn normalized(&self) -> Self {
        let len = self.length();
        if len > 0.0 {
            Self { x: self.x / len, y: self.y / len }
        } else {
            Self::ZERO
        }
    }
    
    pub fn scale(&self, factor: f32) -> Self {
        Self { x: self.x * factor, y: self.y * factor }
    }
}

/// Motion blur configuration
#[derive(Debug, Clone)]
pub struct MotionBlur {
    /// Velocity vector (direction and magnitude)
    pub velocity: Vec2,
    /// Number of samples (4-16 recommended, higher = better quality)
    pub samples: u32,
    /// Blur intensity (0.0-1.0)
    pub intensity: f32,
    /// Falloff curve (1.0 = linear, >1 = faster falloff at edges)
    pub falloff: f32,
}

impl Default for MotionBlur {
    fn default() -> Self {
        Self {
            velocity: Vec2::ZERO,
            samples: 8,
            intensity: 1.0,
            falloff: 1.5,
        }
    }
}

impl MotionBlur {
    /// Create a new motion blur effect
    pub fn new(velocity: Vec2) -> Self {
        Self {
            velocity,
            ..Default::default()
        }
    }
    
    /// Create motion blur from angle (radians) and speed
    pub fn from_angle(angle: f32, speed: f32) -> Self {
        Self {
            velocity: Vec2::new(angle.cos() * speed, angle.sin() * speed),
            ..Default::default()
        }
    }
    
    /// Create horizontal motion blur
    pub fn horizontal(speed: f32) -> Self {
        Self::new(Vec2::new(speed, 0.0))
    }
    
    /// Create vertical motion blur
    pub fn vertical(speed: f32) -> Self {
        Self::new(Vec2::new(0.0, speed))
    }
    
    /// Set number of samples
    pub fn samples(mut self, count: u32) -> Self {
        self.samples = count.clamp(2, 32);
        self
    }
    
    /// Set intensity
    pub fn intensity(mut self, value: f32) -> Self {
        self.intensity = value.clamp(0.0, 1.0);
        self
    }
    
    /// Set falloff curve
    pub fn falloff(mut self, value: f32) -> Self {
        self.falloff = value.clamp(0.5, 4.0);
        self
    }
    
    /// Check if blur is significant enough to render
    pub fn is_active(&self) -> bool {
        self.velocity.length() > 0.5 && self.intensity > 0.01
    }
    
    /// Calculate sample offsets and weights
    pub fn sample_offsets(&self) -> Vec<(Vec2, f32)> {
        let mut samples = Vec::with_capacity(self.samples as usize);
        let dir = self.velocity.normalized();
        let length = self.velocity.length() * self.intensity;
        
        for i in 0..self.samples {
            // Position along blur line (-0.5 to 0.5)
            let t = (i as f32 / (self.samples - 1) as f32) - 0.5;
            
            // Offset in pixels
            let offset = Vec2::new(
                dir.x * t * length,
                dir.y * t * length,
            );
            
            // Weight with falloff (center samples weighted more)
            let weight = (1.0 - (t * 2.0).abs()).powf(self.falloff);
            
            samples.push((offset, weight));
        }
        
        // Normalize weights
        let total_weight: f32 = samples.iter().map(|(_, w)| w).sum();
        if total_weight > 0.0 {
            for (_, w) in &mut samples {
                *w /= total_weight;
            }
        }
        
        samples
    }
    
    /// Calculate expanded bounds to accommodate blur
    pub fn expanded_bounds(&self, rect: Rect) -> Rect {
        let blur_length = self.velocity.length() * self.intensity;
        let expand_x = (self.velocity.x.abs() / 2.0 * self.intensity).ceil();
        let expand_y = (self.velocity.y.abs() / 2.0 * self.intensity).ceil();
        
        Rect::new(
            rect.x - expand_x,
            rect.y - expand_y,
            rect.width + expand_x * 2.0,
            rect.height + expand_y * 2.0,
        )
    }
}

/// Apply motion blur to pixel buffer (CPU implementation)
pub fn apply_motion_blur(
    input: &[u8],           // Input pixels (RGBA)
    output: &mut [u8],      // Output buffer (RGBA)
    width: u32,
    height: u32,
    blur: &MotionBlur,
) {
    if !blur.is_active() {
        output.copy_from_slice(input);
        return;
    }
    
    let samples = blur.sample_offsets();
    
    for y in 0..height {
        for x in 0..width {
            let mut r = 0.0f32;
            let mut g = 0.0f32;
            let mut b = 0.0f32;
            let mut a = 0.0f32;
            
            for (offset, weight) in &samples {
                let sx = (x as f32 + offset.x).round() as i32;
                let sy = (y as f32 + offset.y).round() as i32;
                
                // Clamp to bounds
                let sx = sx.clamp(0, width as i32 - 1) as u32;
                let sy = sy.clamp(0, height as i32 - 1) as u32;
                
                let idx = ((sy * width + sx) * 4) as usize;
                
                r += input[idx] as f32 * weight;
                g += input[idx + 1] as f32 * weight;
                b += input[idx + 2] as f32 * weight;
                a += input[idx + 3] as f32 * weight;
            }
            
            let out_idx = ((y * width + x) * 4) as usize;
            output[out_idx] = r.clamp(0.0, 255.0) as u8;
            output[out_idx + 1] = g.clamp(0.0, 255.0) as u8;
            output[out_idx + 2] = b.clamp(0.0, 255.0) as u8;
            output[out_idx + 3] = a.clamp(0.0, 255.0) as u8;
        }
    }
}

/// Radial motion blur (zoom blur effect)
#[derive(Debug, Clone)]
pub struct RadialBlur {
    /// Center point of the blur
    pub center: Point,
    /// Blur strength (positive = zoom in, negative = zoom out)
    pub strength: f32,
    /// Number of samples
    pub samples: u32,
}

impl RadialBlur {
    pub fn new(center: Point, strength: f32) -> Self {
        Self {
            center,
            strength,
            samples: 8,
        }
    }
    
    pub fn samples(mut self, count: u32) -> Self {
        self.samples = count.clamp(2, 32);
        self
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_motion_blur_creation() {
        let blur = MotionBlur::horizontal(20.0)
            .samples(12)
            .intensity(0.8);
        
        assert_eq!(blur.velocity.x, 20.0);
        assert_eq!(blur.velocity.y, 0.0);
        assert_eq!(blur.samples, 12);
        assert!((blur.intensity - 0.8).abs() < 0.01);
    }
    
    #[test]
    fn test_sample_offsets() {
        let blur = MotionBlur::horizontal(10.0).samples(5);
        let offsets = blur.sample_offsets();
        
        assert_eq!(offsets.len(), 5);
        
        // Weights should sum to 1.0
        let total_weight: f32 = offsets.iter().map(|(_, w)| w).sum();
        assert!((total_weight - 1.0).abs() < 0.01);
    }
    
    #[test]
    fn test_inactive_blur() {
        let blur = MotionBlur::new(Vec2::ZERO);
        assert!(!blur.is_active());
        
        let small_blur = MotionBlur::horizontal(0.1);
        assert!(!small_blur.is_active());
    }
    
    #[test]
    fn test_from_angle() {
        let blur = MotionBlur::from_angle(0.0, 10.0);
        assert!((blur.velocity.x - 10.0).abs() < 0.01);
        assert!(blur.velocity.y.abs() < 0.01);
    }
}
