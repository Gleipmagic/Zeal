#include "colorblind.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "commands.h"
#include "directx.h"
#include "d3dx8/d3dx8.h"
#include "game_functions.h"
#include "string_util.h"
#include "zeal.h"

namespace {
struct ScreenVertex {
  float x, y, z, rhw;
  float u, v;
};

constexpr DWORD kScreenFVF = D3DFVF_XYZRHW | D3DFVF_TEX1;

// These matrices are accessibility-oriented colour remaps, not simulations.
// They deliberately move information out of the confused axis while retaining
// luminance and as much of the original image as possible. Strength blends the
// matrix back toward identity.
constexpr float kProtan[3][3] = {
    {0.5667f, 0.4333f, 0.0000f},
    {0.5583f, 0.4417f, 0.0000f},
    {0.0000f, 0.2417f, 0.7583f},
};

constexpr float kDeutan[3][3] = {
    {0.6250f, 0.3750f, 0.0000f},
    {0.7000f, 0.3000f, 0.0000f},
    {0.0000f, 0.3000f, 0.7000f},
};

constexpr float kTritan[3][3] = {
    {0.9500f, 0.0500f, 0.0000f},
    {0.0000f, 0.4333f, 0.5667f},
    {0.0000f, 0.4750f, 0.5250f},
};

// Convert a deficiency-simulation matrix into a simple daltonization-style
// enhancement matrix. Error in the weak channel is pushed toward the other
// two channels. This is intentionally conservative for an initial DX8 test.
void MakeAssistMatrix(const float sim[3][3], int mode, float out[3][3]) {
  float correction[3][3] = {};
  if (mode == 1) {  // protan: red information -> green/blue
    correction[1][0] = 0.70f;
    correction[2][0] = 0.70f;
  } else if (mode == 2) {  // deutan: green information -> red/blue
    correction[0][1] = 0.70f;
    correction[2][1] = 0.70f;
  } else {  // tritan: blue information -> red/green
    correction[0][2] = 0.70f;
    correction[1][2] = 0.70f;
  }

  // assist = I + correction * (I - simulation)
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float value = (r == c) ? 1.0f : 0.0f;
      for (int k = 0; k < 3; ++k) {
        const float identity_minus_sim = ((k == c) ? 1.0f : 0.0f) - sim[k][c];
        value += correction[r][k] * identity_minus_sim;
      }
      out[r][c] = value;
    }
  }
}
}  // namespace

ColorblindFilter::ColorblindFilter(ZealService *zeal) {
  zeal->commands_hook->Add(
      "/colorblind", {"/cb"},
      "Colour-blind accessibility filter. Usage: /colorblind off|protan|deutan|tritan|strength <0-100>|status",
      [this](std::vector<std::string> &args) {
        if (args.size() == 1 || (args.size() == 2 && Zeal::String::compare_insensitive(args[1], "status"))) {
          PrintStatus();
          return true;
        }

        if (args.size() == 2) {
          int new_mode = -1;
          if (Zeal::String::compare_insensitive(args[1], "off")) new_mode = 0;
          if (Zeal::String::compare_insensitive(args[1], "protan") ||
              Zeal::String::compare_insensitive(args[1], "protanopia"))
            new_mode = 1;
          if (Zeal::String::compare_insensitive(args[1], "deutan") ||
              Zeal::String::compare_insensitive(args[1], "deuteranopia"))
            new_mode = 2;
          if (Zeal::String::compare_insensitive(args[1], "tritan") ||
              Zeal::String::compare_insensitive(args[1], "tritanopia"))
            new_mode = 3;

          if (new_mode >= 0) {
            mode.set(new_mode);
            Zeal::Game::print_chat("Zeal: Colour Blind Mode set to %s.", ModeName());
            return true;
          }
        }

        if (args.size() == 3 && Zeal::String::compare_insensitive(args[1], "strength")) {
          int value = -1;
          if (Zeal::String::tryParse(args[2], &value, true) && value >= 0 && value <= 100) {
            strength.set(value);
            Zeal::Game::print_chat("Zeal: Colour Blind strength set to %d%%.", value);
            return true;
          }
          Zeal::Game::print_chat("Zeal: Colour Blind strength must be between 0 and 100.");
          return true;
        }

        Zeal::Game::print_chat(
            "Usage: /colorblind off|protan|deutan|tritan, /colorblind strength <0-100>, /colorblind status");
        return true;
      });
}

ColorblindFilter::~ColorblindFilter() { ReleaseResources(); }

const char *ColorblindFilter::ModeName() const {
  switch (mode.get()) {
    case 1:
      return "Protan";
    case 2:
      return "Deutan";
    case 3:
      return "Tritan";
    default:
      return "Off";
  }
}

void ColorblindFilter::PrintStatus() const {
  Zeal::Game::print_chat("Zeal: Colour Blind Mode: %s, strength: %d%%.", ModeName(), strength.get());
}

void ColorblindFilter::BuildMatrix(float out[3][4]) const {
  const int active_mode = mode.get();
  const float(*simulation)[3] = kProtan;
  if (active_mode == 2) simulation = kDeutan;
  if (active_mode == 3) simulation = kTritan;

  float assist[3][3] = {};
  MakeAssistMatrix(simulation, active_mode, assist);

  const float amount = std::clamp(strength.get(), 0, 100) / 100.0f;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      const float identity = (r == c) ? 1.0f : 0.0f;
      out[r][c] = identity + amount * (assist[r][c] - identity);
    }
    out[r][3] = 0.0f;
  }
}

bool ColorblindFilter::EnsureShader(IDirect3DDevice8 *device) {
  if (pixel_shader_ != 0) return true;

  static const char shader_source[] =
      "ps.1.1\n"
      "tex t0\n"
      "dp3 r0.r, t0, c0\n"
      "dp3 r0.g, t0, c1\n"
      "dp3 r0.b, t0, c2\n"
      "mov r0.a, t0.a\n";

  LPD3DXBUFFER compiled = nullptr;
  LPD3DXBUFFER errors = nullptr;
  HRESULT hr = D3DXAssembleShader(shader_source, static_cast<UINT>(std::strlen(shader_source)), 0, nullptr, &compiled,
                                  &errors);
  if (FAILED(hr) || !compiled) {
    if (!warned_shader_failure_) {
      const char *detail = errors ? reinterpret_cast<const char *>(errors->GetBufferPointer()) : "unknown error";
      Zeal::Game::print_chat("Zeal: Colour Blind filter shader could not be created (%s). Feature disabled.", detail);
      warned_shader_failure_ = true;
    }
    if (errors) errors->Release();
    if (compiled) compiled->Release();
    return false;
  }

  hr = device->CreatePixelShader(reinterpret_cast<const DWORD *>(compiled->GetBufferPointer()), &pixel_shader_);
  compiled->Release();
  if (errors) errors->Release();
  if (FAILED(hr)) {
    pixel_shader_ = 0;
    if (!warned_shader_failure_) {
      Zeal::Game::print_chat("Zeal: Colour Blind pixel shader is not supported by this renderer. Feature disabled.");
      warned_shader_failure_ = true;
    }
    return false;
  }
  return true;
}

bool ColorblindFilter::EnsureResources(IDirect3DDevice8 *device, UINT width, UINT height, D3DFORMAT format) {
  if (scene_texture_ && width == texture_width_ && height == texture_height_ && format == texture_format_)
    return EnsureShader(device);

  ReleaseResources();
  HRESULT hr = device->CreateTexture(width, height, 1, 0, format, D3DPOOL_DEFAULT, &scene_texture_);
  if (FAILED(hr) || !scene_texture_) {
    // X8R8G8B8 is more widely accepted for a copied backbuffer than some display formats.
    hr = device->CreateTexture(width, height, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &scene_texture_);
    format = D3DFMT_X8R8G8B8;
  }
  if (FAILED(hr) || !scene_texture_) return false;

  texture_width_ = width;
  texture_height_ = height;
  texture_format_ = format;
  return EnsureShader(device);
}

void ColorblindFilter::ReleaseResources() {
  if (scene_texture_) {
    scene_texture_->Release();
    scene_texture_ = nullptr;
  }
  if (pixel_shader_ != 0) {
    IDirect3DDevice8 *device = ZealService::get_instance() && ZealService::get_instance()->dx
                                   ? ZealService::get_instance()->dx->GetDevice()
                                   : nullptr;
    if (device) device->DeletePixelShader(pixel_shader_);
    pixel_shader_ = 0;
  }
  texture_width_ = 0;
  texture_height_ = 0;
  texture_format_ = D3DFMT_UNKNOWN;
}

void ColorblindFilter::OnDeviceLost() { ReleaseResources(); }

void ColorblindFilter::Apply(IDirect3DDevice8 *device) {
  if (!device || mode.get() <= 0 || mode.get() > 3 || strength.get() <= 0) return;

  IDirect3DSurface8 *backbuffer = nullptr;
  if (FAILED(device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) || !backbuffer) return;

  D3DSURFACE_DESC desc = {};
  backbuffer->GetDesc(&desc);
  if (!EnsureResources(device, desc.Width, desc.Height, desc.Format)) {
    backbuffer->Release();
    return;
  }

  IDirect3DSurface8 *texture_surface = nullptr;
  if (FAILED(scene_texture_->GetSurfaceLevel(0, &texture_surface)) || !texture_surface) {
    backbuffer->Release();
    return;
  }

  HRESULT copy_result = device->CopyRects(backbuffer, nullptr, 0, texture_surface, nullptr);
  texture_surface->Release();
  if (FAILED(copy_result)) {
    if (!warned_copy_failure_) {
      Zeal::Game::print_chat(
          "Zeal: Colour Blind full-screen copy is not supported by this DX8 renderer. Filter left off for safety.");
      warned_copy_failure_ = true;
    }
    backbuffer->Release();
    return;
  }

  DWORD old_vertex_shader = 0;
  DWORD old_pixel_shader = 0;
  IDirect3DBaseTexture8 *old_texture = nullptr;
  DWORD old_z_enable = FALSE;
  DWORD old_alpha_blend = FALSE;
  DWORD old_cull = D3DCULL_CCW;
  DWORD old_lighting = FALSE;
  DWORD old_color_op = 0;
  DWORD old_color_arg1 = 0;
  DWORD old_alpha_op = 0;
  DWORD old_alpha_arg1 = 0;

  device->GetVertexShader(&old_vertex_shader);
  device->GetPixelShader(&old_pixel_shader);
  device->GetTexture(0, &old_texture);
  device->GetRenderState(D3DRS_ZENABLE, &old_z_enable);
  device->GetRenderState(D3DRS_ALPHABLENDENABLE, &old_alpha_blend);
  device->GetRenderState(D3DRS_CULLMODE, &old_cull);
  device->GetRenderState(D3DRS_LIGHTING, &old_lighting);
  device->GetTextureStageState(0, D3DTSS_COLOROP, &old_color_op);
  device->GetTextureStageState(0, D3DTSS_COLORARG1, &old_color_arg1);
  device->GetTextureStageState(0, D3DTSS_ALPHAOP, &old_alpha_op);
  device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &old_alpha_arg1);

  float matrix[3][4] = {};
  BuildMatrix(matrix);
  device->SetPixelShaderConstant(0, matrix, 3);
  device->SetTexture(0, scene_texture_);
  device->SetVertexShader(kScreenFVF);
  device->SetPixelShader(pixel_shader_);
  device->SetRenderState(D3DRS_ZENABLE, FALSE);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  device->SetRenderState(D3DRS_LIGHTING, FALSE);
  device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
  device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

  const float w = static_cast<float>(desc.Width);
  const float h = static_cast<float>(desc.Height);
  // Half-pixel offset is required for DX8 pre-transformed fullscreen quads.
  ScreenVertex vertices[4] = {
      {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f},
      {w - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f},
      {-0.5f, h - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f},
      {w - 0.5f, h - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f},
  };
  device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(ScreenVertex));

  device->SetPixelShader(old_pixel_shader);
  device->SetVertexShader(old_vertex_shader);
  device->SetTexture(0, old_texture);
  device->SetRenderState(D3DRS_ZENABLE, old_z_enable);
  device->SetRenderState(D3DRS_ALPHABLENDENABLE, old_alpha_blend);
  device->SetRenderState(D3DRS_CULLMODE, old_cull);
  device->SetRenderState(D3DRS_LIGHTING, old_lighting);
  device->SetTextureStageState(0, D3DTSS_COLOROP, old_color_op);
  device->SetTextureStageState(0, D3DTSS_COLORARG1, old_color_arg1);
  device->SetTextureStageState(0, D3DTSS_ALPHAOP, old_alpha_op);
  device->SetTextureStageState(0, D3DTSS_ALPHAARG1, old_alpha_arg1);
  if (old_texture) old_texture->Release();
  backbuffer->Release();
}
