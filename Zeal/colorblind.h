#pragma once

#include "d3dx8/d3d8.h"
#include "zeal_settings.h"

class ColorblindFilter {
 public:
  explicit ColorblindFilter(class ZealService *zeal);
  ~ColorblindFilter();

  // Applies a full-screen colour remap to the current DirectX 8 backbuffer.
  // Safe no-op when disabled or when the device does not support the pass.
  void Apply(IDirect3DDevice8 *device);
  void OnDeviceLost();

  ZealSetting<int> mode = {0, "Colorblind", "Mode", false};       // 0 off, 1 protan, 2 deutan, 3 tritan
  ZealSetting<int> strength = {75, "Colorblind", "Strength", false};  // 0..100

 private:
  bool EnsureResources(IDirect3DDevice8 *device, UINT width, UINT height, D3DFORMAT format);
  bool EnsureShader(IDirect3DDevice8 *device);
  void ReleaseResources();
  void PrintStatus() const;
  const char *ModeName() const;
  void BuildMatrix(float out[3][4]) const;

  IDirect3DTexture8 *scene_texture_ = nullptr;
  UINT texture_width_ = 0;
  UINT texture_height_ = 0;
  D3DFORMAT texture_format_ = D3DFMT_UNKNOWN;
  DWORD pixel_shader_ = 0;
  bool warned_copy_failure_ = false;
  bool warned_shader_failure_ = false;
};
