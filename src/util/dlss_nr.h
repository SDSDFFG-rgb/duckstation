// SPDX-FileCopyrightText: 2019-2026 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#pragma once

#include "common/types.h"
#include "util/gpu_texture.h"

#include <memory>
#include <string>

class Error;

/// DLSS 5 Neural Rendering (NVIDIA NGX feature 18) processor.
///
/// Applies neural rendering to the final scaled game image on the Direct3D 12 backend.
/// Requires a user-supplied runtime (nvngx_dlssnr.dll) plus the caller-validation shim
/// (nvngx.dll) next to the executable; the NGX core (_nvngx.dll) is discovered from the
/// installed driver. When any of these components are missing or initialization fails,
/// the processor reports unavailable and normal rendering is used.
class DLSSNRProcessor
{
public:
  /// User-tunable NR parameters, applied when the feature is created.
  struct Parameters
  {
    int style = 1;
    int preset = 3;
    float intensity = 1.0f;
    float tone = 0.0f;
    float structure = 1.0f;
    float skin_structure = -1.0f;
    bool auto_mask = false;
    bool pgxp_depth = false;
  };

  static DLSSNRProcessor& GetInstance();

  DLSSNRProcessor(const DLSSNRProcessor&) = delete;
  DLSSNRProcessor& operator=(const DLSSNRProcessor&) = delete;

  /// Sets the user parameters used for feature creation. Call before Initialize().
  void SetParameters(const Parameters& params);

  /// Enables/disables NR processing. Call before Initialize(); changing after initialization
  /// requires Shutdown() first.
  void SetEnabled(bool enabled);

  /// Loads the NGX runtime libraries and initializes NGX against the current device.
  /// Safe to call repeatedly; re-initializes if the D3D12 device changes.
  bool Initialize(const std::string& data_path, Error* error);

  /// Releases the feature, parameter object and unloads all libraries.
  void Shutdown();

  ALWAYS_INLINE bool IsEnabled() const { return m_enabled; }
  ALWAYS_INLINE bool IsInitialized() const { return m_initialized; }
  ALWAYS_INLINE bool IsAvailable() const { return (m_enabled && m_initialized && !m_disabled); }

  /// Ensures the intermediate textures and the NGX feature match the specified size.
  /// The feature is recreated and temporal history reset when the size changes.
  bool Resize(u32 width, u32 height, Error* error);

  ALWAYS_INLINE GPUTexture* GetInputTexture() const { return m_input_texture.get(); }
  ALWAYS_INLINE GPUTexture* GetOutputTexture() const { return m_output_texture.get(); }
  ALWAYS_INLINE GPUTexture* GetDepthTexture() const { return m_depth_texture.get(); }

  /// Records the neural rendering evaluation into the current D3D12 command list.
  /// The color textures must be RGBA16F, the optional depth texture must be R32F,
  /// and all textures must be sized to the last Resize() call. Submits the command
  /// list afterwards to invalidate cached device state.
  bool Process(GPUTexture* input, GPUTexture* output, GPUTexture* depth, bool reset_history, Error* error);

  /// Returns true if a temporal history reset is pending, and clears the pending flag.
  bool ConsumeHistoryReset();

  /// Forces a temporal history reset on the next Process() call.
  void RequestHistoryReset();

  /// Disables NR processing for the remainder of the session after an unrecoverable error.
  void DisableForSession();

private:
  DLSSNRProcessor();
  ~DLSSNRProcessor();

  bool LoadLibraries(Error* error);
  bool InitializeNGX(const std::string& data_path, Error* error);
  bool CreateFeature(Error* error);
  void DestroyFeature();
  void DestroyTextures();
  void UnloadLibraries();

  bool m_enabled = false;
  bool m_initialized = false;
  bool m_disabled = false;
  bool m_history_reset_needed = true;
  u32 m_width = 0;
  u32 m_height = 0;
  Parameters m_user_params;
  Parameters m_applied_params;
  bool m_params_dirty = false;

  std::unique_ptr<GPUTexture> m_input_texture;
  std::unique_ptr<GPUTexture> m_output_texture;
  std::unique_ptr<GPUTexture> m_depth_texture;

  // Opaque NGX objects and module/function pointers, only used on Windows.
  void* m_device = nullptr;
  void* m_params = nullptr;
  void* m_feature = nullptr;
  void* m_core_module = nullptr;
  void* m_nr_module = nullptr;
  void* m_shim_module = nullptr;
  void* m_fn_init_project_id = nullptr;
  void* m_fn_allocate_parameters = nullptr;
  void* m_fn_destroy_parameters = nullptr;
  void* m_fn_create_feature = nullptr;
  void* m_fn_evaluate_feature = nullptr;
  void* m_fn_release_feature = nullptr;
  void* m_fn_shutdown = nullptr;
  void* m_fn_nr_init_ext = nullptr;
  void* m_fn_nr_create_feature = nullptr;
  void* m_fn_nr_evaluate_feature = nullptr;
  void* m_fn_nr_release_feature = nullptr;
  void* m_fn_shim_init = nullptr;
  void* m_fn_shim_create = nullptr;
  void* m_fn_shim_evaluate = nullptr;
  void* m_fn_shim_release = nullptr;
};
