//! Visual Effects Module
//!
//! Provides GPU-accelerated effects like gradients,
//! shadows, blur, glassmorphism, motion blur, and 3D lighting.

pub mod gradient;
pub mod shadow;
pub mod blur;
pub mod glassmorphism;
pub mod motion_blur;
pub mod lighting3d;

// Existing exports
pub use gradient::{LinearGradient, RadialGradient, ColorStop, GradientDirection};
pub use shadow::{Shadow, BoxShadows};
pub use blur::{Blur, BlurQuality, box_blur_rgba, box_blur_rgba_multi};

// New modern UI effects
pub use glassmorphism::{Glassmorphism, apply_glassmorphism};
pub use motion_blur::{MotionBlur, RadialBlur, Vec2, apply_motion_blur};
pub use lighting3d::{LightingSystem, Light, Material, Vec3};

