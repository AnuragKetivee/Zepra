//! 3D Lighting System
//!
//! Provides ambient, directional, point, and spot lighting for UI depth effects.
//! Used in NeolyxOS for realistic UI rendering.

use crate::primitives::Color;

/// 3D Vector for positions and directions
#[derive(Debug, Clone, Copy, Default)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0 };
    pub const UP: Self = Self { x: 0.0, y: -1.0, z: 0.0 };
    pub const DOWN: Self = Self { x: 0.0, y: 1.0, z: 0.0 };
    pub const FORWARD: Self = Self { x: 0.0, y: 0.0, z: 1.0 };
    
    pub fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }
    
    pub fn length(&self) -> f32 {
        (self.x * self.x + self.y * self.y + self.z * self.z).sqrt()
    }
    
    pub fn normalized(&self) -> Self {
        let len = self.length();
        if len > 0.0 {
            Self { x: self.x / len, y: self.y / len, z: self.z / len }
        } else {
            Self::ZERO
        }
    }
    
    pub fn dot(&self, other: &Self) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z
    }
    
    pub fn scale(&self, factor: f32) -> Self {
        Self { x: self.x * factor, y: self.y * factor, z: self.z * factor }
    }
    
    pub fn sub(&self, other: &Self) -> Self {
        Self { x: self.x - other.x, y: self.y - other.y, z: self.z - other.z }
    }
    
    pub fn add(&self, other: &Self) -> Self {
        Self { x: self.x + other.x, y: self.y + other.y, z: self.z + other.z }
    }
    
    /// Reflect vector around normal
    pub fn reflect(&self, normal: &Self) -> Self {
        let d = 2.0 * self.dot(normal);
        self.sub(&normal.scale(d))
    }
}

/// Light types
#[derive(Debug, Clone)]
pub enum Light {
    /// Ambient light - uniform illumination everywhere
    Ambient {
        color: Color,
        intensity: f32,
    },
    /// Directional light - parallel rays (like sun)
    Directional {
        direction: Vec3,
        color: Color,
        intensity: f32,
    },
    /// Point light - radiates in all directions from a point
    Point {
        position: Vec3,
        color: Color,
        intensity: f32,
        radius: f32,
        falloff: f32,
    },
    /// Spot light - cone of light
    Spot {
        position: Vec3,
        direction: Vec3,
        color: Color,
        intensity: f32,
        inner_angle: f32,  // Full brightness cone (radians)
        outer_angle: f32,  // Falloff cone (radians)
        radius: f32,
    },
}

impl Light {
    /// Create ambient light
    pub fn ambient(color: Color, intensity: f32) -> Self {
        Light::Ambient { color, intensity }
    }
    
    /// Create directional light (sun-like)
    pub fn directional(direction: Vec3, color: Color, intensity: f32) -> Self {
        Light::Directional {
            direction: direction.normalized(),
            color,
            intensity,
        }
    }
    
    /// Create point light
    pub fn point(position: Vec3, color: Color, intensity: f32, radius: f32) -> Self {
        Light::Point {
            position,
            color,
            intensity,
            radius,
            falloff: 2.0,  // Quadratic falloff
        }
    }
    
    /// Create spot light
    pub fn spot(position: Vec3, direction: Vec3, color: Color, intensity: f32, angle: f32) -> Self {
        Light::Spot {
            position,
            direction: direction.normalized(),
            color,
            intensity,
            inner_angle: angle * 0.8,
            outer_angle: angle,
            radius: 1000.0,
        }
    }
    
    /// Get light color scaled by intensity
    pub fn effective_color(&self) -> Color {
        match self {
            Light::Ambient { color, intensity } |
            Light::Directional { color, intensity, .. } |
            Light::Point { color, intensity, .. } |
            Light::Spot { color, intensity, .. } => {
                Color::rgba(
                    ((color.r as f32 * intensity).min(255.0)) as u8,
                    ((color.g as f32 * intensity).min(255.0)) as u8,
                    ((color.b as f32 * intensity).min(255.0)) as u8,
                    color.a,
                )
            }
        }
    }
}

/// Material properties for lit surfaces
#[derive(Debug, Clone)]
pub struct Material {
    /// Base/diffuse color
    pub diffuse: Color,
    /// Ambient color (usually same as diffuse but dimmer)
    pub ambient: Color,
    /// Specular highlight color
    pub specular: Color,
    /// Shininess (higher = sharper highlights, 1-256)
    pub shininess: f32,
    /// Emissive color (self-illumination)
    pub emissive: Color,
}

impl Default for Material {
    fn default() -> Self {
        Self {
            diffuse: Color::rgb(200, 200, 200),
            ambient: Color::rgb(50, 50, 50),
            specular: Color::rgb(255, 255, 255),
            shininess: 32.0,
            emissive: Color::TRANSPARENT,
        }
    }
}

impl Material {
    pub fn new(diffuse: Color) -> Self {
        Self {
            diffuse,
            ambient: Color::rgba(
                diffuse.r / 4,
                diffuse.g / 4,
                diffuse.b / 4,
                diffuse.a,
            ),
            ..Default::default()
        }
    }
    
    /// Matte/non-reflective material
    pub fn matte(color: Color) -> Self {
        Self {
            diffuse: color,
            ambient: Color::rgba(color.r / 4, color.g / 4, color.b / 4, color.a),
            specular: Color::rgb(30, 30, 30),
            shininess: 4.0,
            emissive: Color::TRANSPARENT,
        }
    }
    
    /// Glossy material (plastic-like)
    pub fn glossy(color: Color) -> Self {
        Self {
            diffuse: color,
            ambient: Color::rgba(color.r / 4, color.g / 4, color.b / 4, color.a),
            specular: Color::rgb(200, 200, 200),
            shininess: 64.0,
            emissive: Color::TRANSPARENT,
        }
    }
    
    /// Metallic material
    pub fn metallic(color: Color) -> Self {
        Self {
            diffuse: color,
            ambient: Color::rgba(color.r / 5, color.g / 5, color.b / 5, color.a),
            specular: color,  // Metallic reflection is tinted
            shininess: 128.0,
            emissive: Color::TRANSPARENT,
        }
    }
    
    /// Glowing/emissive material
    pub fn glowing(color: Color) -> Self {
        Self {
            diffuse: color,
            ambient: color,
            specular: Color::rgb(255, 255, 255),
            shininess: 16.0,
            emissive: color,
        }
    }
}

/// 3D Lighting system
#[derive(Debug, Clone)]
pub struct LightingSystem {
    /// List of lights in the scene
    lights: Vec<Light>,
    /// Global ambient light
    global_ambient: Color,
    /// Enable/disable specular highlights
    specular_enabled: bool,
}

impl Default for LightingSystem {
    fn default() -> Self {
        Self::new()
    }
}

impl LightingSystem {
    pub fn new() -> Self {
        Self {
            lights: vec![
                Light::ambient(Color::rgb(40, 40, 45), 1.0),
                Light::directional(Vec3::new(0.5, -1.0, 0.8), Color::rgb(255, 250, 245), 0.8),
            ],
            global_ambient: Color::rgb(20, 20, 25),
            specular_enabled: true,
        }
    }
    
    /// Create a minimal lighting setup
    pub fn minimal() -> Self {
        Self {
            lights: vec![
                Light::ambient(Color::rgb(80, 80, 80), 1.0),
            ],
            global_ambient: Color::rgb(30, 30, 30),
            specular_enabled: false,
        }
    }
    
    /// Create a dramatic lighting setup
    pub fn dramatic() -> Self {
        Self {
            lights: vec![
                Light::ambient(Color::rgb(10, 10, 15), 1.0),
                Light::directional(Vec3::new(1.0, -0.5, 0.5), Color::rgb(255, 200, 150), 1.2),
                Light::directional(Vec3::new(-1.0, 0.5, -0.5), Color::rgb(100, 150, 255), 0.3),
            ],
            global_ambient: Color::rgb(5, 5, 10),
            specular_enabled: true,
        }
    }
    
    /// Add a light
    pub fn add_light(&mut self, light: Light) {
        self.lights.push(light);
    }
    
    /// Clear all lights
    pub fn clear(&mut self) {
        self.lights.clear();
    }
    
    /// Get lights
    pub fn lights(&self) -> &[Light] {
        &self.lights
    }
    
    /// Calculate lighting for a point with given normal
    pub fn calculate(
        &self,
        position: Vec3,
        normal: Vec3,
        view_dir: Vec3,
        material: &Material,
    ) -> Color {
        let normal = normal.normalized();
        let view_dir = view_dir.normalized();
        
        let mut r = self.global_ambient.r as f32 + material.emissive.r as f32;
        let mut g = self.global_ambient.g as f32 + material.emissive.g as f32;
        let mut b = self.global_ambient.b as f32 + material.emissive.b as f32;
        
        for light in &self.lights {
            match light {
                Light::Ambient { color, intensity } => {
                    r += material.ambient.r as f32 * color.r as f32 / 255.0 * intensity;
                    g += material.ambient.g as f32 * color.g as f32 / 255.0 * intensity;
                    b += material.ambient.b as f32 * color.b as f32 / 255.0 * intensity;
                }
                
                Light::Directional { direction, color, intensity } => {
                    let light_dir = direction.scale(-1.0);
                    let diffuse_factor = normal.dot(&light_dir).max(0.0) * intensity;
                    
                    r += material.diffuse.r as f32 * color.r as f32 / 255.0 * diffuse_factor;
                    g += material.diffuse.g as f32 * color.g as f32 / 255.0 * diffuse_factor;
                    b += material.diffuse.b as f32 * color.b as f32 / 255.0 * diffuse_factor;
                    
                    if self.specular_enabled {
                        let reflect_dir = light_dir.scale(-1.0).reflect(&normal);
                        let spec_factor = view_dir.dot(&reflect_dir).max(0.0).powf(material.shininess) * intensity;
                        
                        r += material.specular.r as f32 * color.r as f32 / 255.0 * spec_factor;
                        g += material.specular.g as f32 * color.g as f32 / 255.0 * spec_factor;
                        b += material.specular.b as f32 * color.b as f32 / 255.0 * spec_factor;
                    }
                }
                
                Light::Point { position: light_pos, color, intensity, radius, falloff } => {
                    let light_vec = light_pos.sub(&position);
                    let distance = light_vec.length();
                    
                    if distance < *radius {
                        let light_dir = light_vec.normalized();
                        let attenuation = (1.0 - (distance / radius).powf(*falloff)).max(0.0);
                        let diffuse_factor = normal.dot(&light_dir).max(0.0) * intensity * attenuation;
                        
                        r += material.diffuse.r as f32 * color.r as f32 / 255.0 * diffuse_factor;
                        g += material.diffuse.g as f32 * color.g as f32 / 255.0 * diffuse_factor;
                        b += material.diffuse.b as f32 * color.b as f32 / 255.0 * diffuse_factor;
                    }
                }
                
                Light::Spot { position: light_pos, direction, color, intensity, inner_angle, outer_angle, radius } => {
                    let light_vec = light_pos.sub(&position);
                    let distance = light_vec.length();
                    
                    if distance < *radius {
                        let light_dir = light_vec.normalized();
                        let spot_dot = light_dir.scale(-1.0).dot(&direction);
                        let spot_inner = inner_angle.cos();
                        let spot_outer = outer_angle.cos();
                        
                        if spot_dot > spot_outer {
                            let spot_factor = ((spot_dot - spot_outer) / (spot_inner - spot_outer)).clamp(0.0, 1.0);
                            let attenuation = 1.0 - (distance / radius);
                            let diffuse_factor = normal.dot(&light_dir).max(0.0) * intensity * attenuation * spot_factor;
                            
                            r += material.diffuse.r as f32 * color.r as f32 / 255.0 * diffuse_factor;
                            g += material.diffuse.g as f32 * color.g as f32 / 255.0 * diffuse_factor;
                            b += material.diffuse.b as f32 * color.b as f32 / 255.0 * diffuse_factor;
                        }
                    }
                }
            }
        }
        
        Color::rgba(
            r.clamp(0.0, 255.0) as u8,
            g.clamp(0.0, 255.0) as u8,
            b.clamp(0.0, 255.0) as u8,
            material.diffuse.a,
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_vec3_operations() {
        let v = Vec3::new(3.0, 4.0, 0.0);
        assert!((v.length() - 5.0).abs() < 0.01);
        
        let n = v.normalized();
        assert!((n.length() - 1.0).abs() < 0.01);
    }
    
    #[test]
    fn test_material_presets() {
        let matte = Material::matte(Color::rgb(100, 100, 100));
        assert!(matte.shininess < 10.0);
        
        let metallic = Material::metallic(Color::rgb(255, 200, 0));
        assert!(metallic.shininess > 100.0);
    }
    
    #[test]
    fn test_lighting_calculation() {
        let lighting = LightingSystem::new();
        let material = Material::default();
        
        let color = lighting.calculate(
            Vec3::ZERO,
            Vec3::UP,
            Vec3::new(0.0, 0.0, -1.0),
            &material,
        );
        
        // Should produce some color
        assert!(color.r > 0 || color.g > 0 || color.b > 0);
    }
}
