// SPDX-FileCopyrightText: 2019-2026 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: CC-BY-NC-ND-4.0

#include "dlss_nr.h"
#include "gpu_device.h"

#include "common/error.h"
#include "common/file_system.h"
#include "common/log.h"
#include "common/path.h"
#include "common/string_util.h"

#include <array>
#include <filesystem>
#include <memory>

LOG_CHANNEL(DLSSNR);

#ifdef _WIN32

#include "common/windows_headers.h"
#include "d3d12_device.h"
#include "d3d12_texture.h"

#include <d3d12.h>

// ---------------------------------------------------------------------------
// Minimal NGX interface. Layouts/signatures mirror the NGX SDK and are verified
// against the nvngx_dlssnr.dll export table; do not guess changes here.
// ---------------------------------------------------------------------------
typedef int NVSDK_NGX_Result;
static constexpr NVSDK_NGX_Result NGX_SUCCESS = 1;

static constexpr int DLSS_NR_FEATURE_ID = 18;
static constexpr unsigned long long DLSS_NR_APP_ID = 141959980ULL;
static const char DLSS_NR_PROJECT_ID[] = "53f803cc-a12f-4d69-90d5-19b7599cad19";

struct NVSDK_NGX_Handle
{
  unsigned int Id;
};

struct NVSDK_NGX_Parameter
{
  virtual void Set(const char* InName, unsigned long long InValue) = 0;
  virtual void Set(const char* InName, float InValue) = 0;
  virtual void Set(const char* InName, double InValue) = 0;
  virtual void Set(const char* InName, unsigned int InValue) = 0;
  virtual void Set(const char* InName, int InValue) = 0;
  virtual void Set(const char* InName, struct ID3D11Resource* InValue) = 0;
  virtual void Set(const char* InName, struct ID3D12Resource* InValue) = 0;
  virtual void Set(const char* InName, void* InValue) = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, unsigned long long* OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, float* OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, double* OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, unsigned int* OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, int* OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, struct ID3D11Resource** OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, struct ID3D12Resource** OutValue) const = 0;
  virtual NVSDK_NGX_Result Get(const char* InName, void** OutValue) const = 0;
  virtual void Reset() = 0;
};

typedef struct NVSDK_NGX_PathListInfo
{
  wchar_t const* const* Path;
  unsigned int Length;
} NVSDK_NGX_PathListInfo;

typedef enum NVSDK_NGX_Logging_Level
{
  NVSDK_NGX_LOGGING_LEVEL_OFF = 0,
  NVSDK_NGX_LOGGING_LEVEL_ON,
  NVSDK_NGX_LOGGING_LEVEL_VERBOSE,
} NVSDK_NGX_Logging_Level;

typedef void(__cdecl* NVSDK_NGX_AppLogCallback)(const char*, NVSDK_NGX_Logging_Level, int);

typedef struct NVSDK_NGX_LoggingInfo
{
  NVSDK_NGX_Logging_Level LoggingLevel;
  NVSDK_NGX_AppLogCallback Callback;
  void* UserData;
  bool DisableOtherLoggingSinks;
} NVSDK_NGX_LoggingInfo;

typedef struct NVSDK_NGX_FeatureCommonInfo_Internal NVSDK_NGX_FeatureCommonInfo_Internal;

typedef struct NVSDK_NGX_FeatureCommonInfo
{
  NVSDK_NGX_PathListInfo PathListInfo;
  NVSDK_NGX_FeatureCommonInfo_Internal* InternalData;
  NVSDK_NGX_LoggingInfo LoggingInfo;
} NVSDK_NGX_FeatureCommonInfo;

typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_Init_ProjectID)(const char* ProjectId, int AppId,
                                                               const char* EngineType, const wchar_t* Path,
                                                               ID3D12Device* Device, int SDKVersion,
                                                               const void* FeatureInfo);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_Init_Ext)(unsigned long long AppId, const wchar_t* Path,
                                                         ID3D12Device* Device, int SDKVersion,
                                                         const void* FeatureInfo);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_DestroyParameters)(NVSDK_NGX_Parameter* Parameters);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_CreateFeature)(ID3D12GraphicsCommandList* CmdList, int FeatureID,
                                                              NVSDK_NGX_Parameter* Parameters,
                                                              NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_EvaluateFeature)(ID3D12GraphicsCommandList* CmdList,
                                                                const NVSDK_NGX_Handle* Handle,
                                                                const NVSDK_NGX_Parameter* Parameters, void* UserData);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_ReleaseFeature)(NVSDK_NGX_Handle* Handle);
typedef NVSDK_NGX_Result (*PFN_NVSDK_NGX_D3D12_Shutdown)(void);

// Caller-validation shim exports (nvngx.dll shim).
typedef NVSDK_NGX_Result (*PFN_ShimInit)(void* InitFn, unsigned long long AppId, const wchar_t* Path,
                                         ID3D12Device* Device, int SDKVersion, const void* FeatureInfo);
typedef NVSDK_NGX_Result (*PFN_ShimCreate)(void* CreateFn, ID3D12GraphicsCommandList* CmdList, int FeatureID,
                                           NVSDK_NGX_Parameter* Parameters, NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_ShimEvaluate)(void* EvaluateFn, ID3D12GraphicsCommandList* CmdList,
                                             const NVSDK_NGX_Handle* Handle, const NVSDK_NGX_Parameter* Parameters,
                                             void* UserData);
typedef NVSDK_NGX_Result (*PFN_ShimRelease)(void* ReleaseFn, NVSDK_NGX_Handle* Handle);

DLSSNRProcessor& DLSSNRProcessor::GetInstance()
{
  static DLSSNRProcessor s_instance;
  return s_instance;
}

DLSSNRProcessor::DLSSNRProcessor() = default;

DLSSNRProcessor::~DLSSNRProcessor()
{
  Shutdown();
}

void DLSSNRProcessor::SetParameters(const Parameters& params)
{
  if (params.style != m_user_params.style || params.preset != m_user_params.preset ||
      params.intensity != m_user_params.intensity || params.tone != m_user_params.tone ||
      params.structure != m_user_params.structure || params.skin_structure != m_user_params.skin_structure ||
      params.auto_mask != m_user_params.auto_mask)
  {
    m_params_dirty = true;
  }
  m_user_params = params;
}

void DLSSNRProcessor::SetEnabled(bool enabled)
{
  m_enabled = enabled;
}

static std::wstring GetExeDirectoryWide()
{
  std::string program_path = FileSystem::GetProgramPath(nullptr);
  const std::string dir(Path::GetDirectory(program_path));
  return StringUtil::UTF8StringToWideString(dir);
}

static HMODULE LoadRuntimeLibrary(const wchar_t* name)
{
  HMODULE mod = ::LoadLibraryW(name);
  if (mod)
    return mod;

  const std::wstring fallback = GetExeDirectoryWide() + L"\\runtime\\" + name;
  return ::LoadLibraryW(fallback.c_str());
}

static HMODULE LoadNGXCoreLibrary()
{
  HMODULE mod = LoadRuntimeLibrary(L"_nvngx.dll");
  if (mod)
    return mod;

  // Discover the NGX core from the installed NVIDIA driver. The INF directory name varies
  // between driver packages (nv_dispi.inf_*, nv_dispig.inf_*, ...), and stale copies from
  // previous driver versions may remain, so use the most recently updated one.
  static constexpr const wchar_t* driver_store = L"C:\\Windows\\System32\\DriverStore\\FileRepository";
  std::error_code err;
  std::filesystem::directory_iterator it(driver_store, err);
  if (err)
    return nullptr;

  std::filesystem::path best;
  std::filesystem::file_time_type best_time{};
  for (const std::filesystem::directory_entry& entry : it)
  {
    if (!entry.is_directory(err))
      continue;

    const std::wstring name = entry.path().filename().wstring();
    if (!name.starts_with(L"nv") || name.find(L".inf_") == std::wstring::npos)
      continue;

    const std::filesystem::path candidate = entry.path() / L"_nvngx.dll";
    if (!std::filesystem::exists(candidate, err) || err)
      continue;

    const std::filesystem::file_time_type time = entry.last_write_time(err);
    if (err)
      continue;

    if (best.empty() || time > best_time)
    {
      best = candidate;
      best_time = time;
    }
  }

  if (best.empty())
    return nullptr;

  return ::LoadLibraryW(best.c_str());
}

static void* GetExport(HMODULE mod, const char* name, const char* what)
{
  void* fn = mod ? reinterpret_cast<void*>(::GetProcAddress(mod, name)) : nullptr;
  if (!fn)
    ERROR_LOG("missing export: {}", what);
  return fn;
}

bool DLSSNRProcessor::LoadLibraries(Error* error)
{
  if (m_core_module)
    return true;

  INFO_LOG("loading _nvngx.dll");
  HMODULE core = LoadNGXCoreLibrary();
  if (!core)
  {
    Error::SetStringView(error, "_nvngx.dll was not found (it should be installed with the NVIDIA driver).");
    ERROR_LOG("LoadLibrary failed: _nvngx.dll");
    return false;
  }

  INFO_LOG("loading nvngx_dlssnr.dll");
  HMODULE nr = LoadRuntimeLibrary(L"nvngx_dlssnr.dll");
  if (!nr)
  {
    Error::SetStringView(
      error, "nvngx_dlssnr.dll was not found. Place a DLSS 5 NR runtime compatible with your GPU in the program "
             "directory (or its 'runtime' subdirectory).");
    ERROR_LOG("LoadLibrary failed: nvngx_dlssnr.dll");
    return false;
  }

  // The NR runtime performs caller validation against a module named nvngx.dll; a thin
  // shim must be provided by the user. Loading it by name also satisfies the NR runtime's
  // own name-based lookup.
  INFO_LOG("loading caller shim nvngx.dll");
  HMODULE shim = LoadRuntimeLibrary(L"nvngx.dll");
  if (!shim)
    WARNING_LOG("caller shim nvngx.dll was not found, direct NGX calls will be attempted");

  m_core_module = core;
  m_nr_module = nr;
  m_shim_module = shim;

  m_fn_init_project_id = GetExport(core, "NVSDK_NGX_D3D12_Init_ProjectID", "NVSDK_NGX_D3D12_Init_ProjectID");
  m_fn_allocate_parameters = GetExport(core, "NVSDK_NGX_D3D12_AllocateParameters", "NVSDK_NGX_D3D12_AllocateParameters");
  m_fn_destroy_parameters = GetExport(core, "NVSDK_NGX_D3D12_DestroyParameters", "NVSDK_NGX_D3D12_DestroyParameters");
  m_fn_create_feature = GetExport(core, "NVSDK_NGX_D3D12_CreateFeature", "NVSDK_NGX_D3D12_CreateFeature");
  m_fn_evaluate_feature = GetExport(core, "NVSDK_NGX_D3D12_EvaluateFeature", "NVSDK_NGX_D3D12_EvaluateFeature");
  m_fn_release_feature = GetExport(core, "NVSDK_NGX_D3D12_ReleaseFeature", "NVSDK_NGX_D3D12_ReleaseFeature");
  m_fn_shutdown = GetExport(core, "NVSDK_NGX_D3D12_Shutdown", "NVSDK_NGX_D3D12_Shutdown");

  m_fn_nr_init_ext = GetExport(nr, "NVSDK_NGX_D3D12_Init_Ext", "NVSDK_NGX_D3D12_Init_Ext (nr)");
  m_fn_nr_create_feature = GetExport(nr, "NVSDK_NGX_D3D12_CreateFeature", "NVSDK_NGX_D3D12_CreateFeature (nr)");
  m_fn_nr_evaluate_feature = GetExport(nr, "NVSDK_NGX_D3D12_EvaluateFeature", "NVSDK_NGX_D3D12_EvaluateFeature (nr)");
  m_fn_nr_release_feature = GetExport(nr, "NVSDK_NGX_D3D12_ReleaseFeature", "NVSDK_NGX_D3D12_ReleaseFeature (nr)");

  if (shim)
  {
    m_fn_shim_init = GetExport(shim, "DLSSNR_CallInit", "DLSSNR_CallInit");
    m_fn_shim_create = GetExport(shim, "DLSSNR_CallCreate", "DLSSNR_CallCreate");
    m_fn_shim_evaluate = GetExport(shim, "DLSSNR_CallEvaluate", "DLSSNR_CallEvaluate");
    m_fn_shim_release = GetExport(shim, "DLSSNR_CallRelease", "DLSSNR_CallRelease");
  }

  if (!m_fn_init_project_id || !m_fn_allocate_parameters || !m_fn_nr_create_feature || !m_fn_nr_evaluate_feature ||
      !m_fn_nr_release_feature)
  {
    Error::SetStringView(error, "A required NGX export is missing; the runtime may be an incompatible version.");
    UnloadLibraries();
    return false;
  }

  INFO_LOG("DLL loaded");
  return true;
}

bool DLSSNRProcessor::InitializeNGX(const std::string& data_path, Error* error)
{
  D3D12Device& dev = D3D12Device::GetInstance();

  const std::wstring wpath = StringUtil::UTF8StringToWideString(data_path);
  const wchar_t* path_list[1] = {wpath.c_str()};
  NVSDK_NGX_PathListInfo pli = {};
  pli.Path = path_list;
  pli.Length = 1;
  NVSDK_NGX_FeatureCommonInfo fci = {};
  fci.PathListInfo = pli;
  fci.LoggingInfo.LoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;

  INFO_LOG("NGX initialization started");

  bool inited = false;
  for (int ver = 0x13; ver <= 0x20 && !inited; ++ver)
  {
    const NVSDK_NGX_Result r = reinterpret_cast<PFN_NVSDK_NGX_D3D12_Init_ProjectID>(m_fn_init_project_id)(
      DLSS_NR_PROJECT_ID, 0, "0.1", wpath.c_str(), dev.GetDevice(), ver, nullptr);
    if (r == NGX_SUCCESS)
    {
      INFO_LOG("core Init_ProjectID version 0x{:02X} ok", ver);
      inited = true;
    }
    else
    {
      DEV_LOG("core Init_ProjectID version 0x{:02X} failed: 0x{:08X}", ver, static_cast<unsigned>(r));
    }
  }
  if (!inited)
  {
    Error::SetStringView(error, "NVSDK_NGX_D3D12_Init_ProjectID() failed. NGX may be unavailable on this system.");
    ERROR_LOG("Init_ProjectID failed");
    return false;
  }

  // Initialize the NR snippet through the caller-validation shim. Failure here is not
  // fatal, the feature creation below will report the actual error.
  if (m_fn_nr_init_ext && m_fn_shim_init)
  {
    const NVSDK_NGX_Result r = reinterpret_cast<PFN_ShimInit>(m_fn_shim_init)(
      m_fn_nr_init_ext, DLSS_NR_APP_ID, wpath.c_str(), dev.GetDevice(), 0x15, &fci);
    INFO_LOG("snippet Init_Ext (via shim) -> 0x{:08X}", static_cast<unsigned>(r));
  }
  else
  {
    WARNING_LOG("caller shim or NR Init_Ext unavailable, skipping snippet initialization");
  }

  NVSDK_NGX_Parameter* params = nullptr;
  const NVSDK_NGX_Result ra =
    reinterpret_cast<PFN_NVSDK_NGX_D3D12_AllocateParameters>(m_fn_allocate_parameters)(&params);
  if (ra != NGX_SUCCESS || !params)
  {
    Error::SetStringFmt(error, "NVSDK_NGX_D3D12_AllocateParameters() failed: 0x{:08X}", static_cast<unsigned>(ra));
    ERROR_LOG("AllocateParameters failed: 0x{:08X}", static_cast<unsigned>(ra));
    return false;
  }
  m_params = params;
  INFO_LOG("NGX initialized");
  return true;
}

bool DLSSNRProcessor::Initialize(const std::string& data_path, Error* error)
{
  if (m_disabled)
  {
    Error::SetStringView(error, "DLSS NR is disabled for this session due to an earlier error.");
    return false;
  }
  if (!m_enabled)
  {
    Error::SetStringView(error, "DLSS NR is not enabled.");
    return false;
  }
  if (!g_gpu_device || g_gpu_device->GetRenderAPI() != RenderAPI::D3D12)
  {
    Error::SetStringView(error, "DLSS NR requires the Direct3D 12 backend.");
    return false;
  }

  void* const cur_device = D3D12Device::GetInstance().GetDevice();
  if (m_initialized && m_device == cur_device)
    return true;

  Shutdown();

  if (!LoadLibraries(error))
    return false;
  if (!InitializeNGX(data_path, error))
  {
    UnloadLibraries();
    return false;
  }

  m_device = cur_device;
  m_initialized = true;
  return true;
}

void DLSSNRProcessor::UnloadLibraries()
{
  m_fn_init_project_id = nullptr;
  m_fn_allocate_parameters = nullptr;
  m_fn_destroy_parameters = nullptr;
  m_fn_create_feature = nullptr;
  m_fn_evaluate_feature = nullptr;
  m_fn_release_feature = nullptr;
  m_fn_shutdown = nullptr;
  m_fn_nr_init_ext = nullptr;
  m_fn_nr_create_feature = nullptr;
  m_fn_nr_evaluate_feature = nullptr;
  m_fn_nr_release_feature = nullptr;
  m_fn_shim_init = nullptr;
  m_fn_shim_create = nullptr;
  m_fn_shim_evaluate = nullptr;
  m_fn_shim_release = nullptr;

  if (m_core_module)
  {
    ::FreeLibrary(static_cast<HMODULE>(m_core_module));
    m_core_module = nullptr;
  }
  if (m_nr_module)
  {
    ::FreeLibrary(static_cast<HMODULE>(m_nr_module));
    m_nr_module = nullptr;
  }
  // Leave the shim loaded once loaded: the NR runtime may retain references to it.
  if (m_shim_module)
    m_shim_module = nullptr;
}

void DLSSNRProcessor::Shutdown()
{
  DestroyFeature();
  DestroyTextures();

  if (m_params && m_fn_destroy_parameters)
    reinterpret_cast<PFN_NVSDK_NGX_D3D12_DestroyParameters>(m_fn_destroy_parameters)(
      static_cast<NVSDK_NGX_Parameter*>(m_params));
  m_params = nullptr;

  if (m_fn_shutdown)
  {
    const NVSDK_NGX_Result r = reinterpret_cast<PFN_NVSDK_NGX_D3D12_Shutdown>(m_fn_shutdown)();
    INFO_LOG("NGX shutdown -> 0x{:08X}", static_cast<unsigned>(r));
    m_fn_shutdown = nullptr;
  }

  m_initialized = false;
  m_device = nullptr;
  m_history_reset_needed = true;
  m_width = 0;
  m_height = 0;
  UnloadLibraries();
}

bool DLSSNRProcessor::Resize(u32 width, u32 height, Error* error)
{
  if (!IsAvailable())
  {
    Error::SetStringView(error, "DLSS NR is not available.");
    return false;
  }
  if (width == 0 || height == 0 || width > GPUTexture::MAX_WIDTH || height > GPUTexture::MAX_HEIGHT)
  {
    Error::SetStringFmt(error, "Invalid DLSS NR size {}x{}", width, height);
    return false;
  }
  if (m_width == width && m_height == height && !m_params_dirty && m_feature && m_input_texture && m_output_texture)
    return true;

  if (m_params_dirty)
    INFO_LOG("parameters changed, recreating feature");

  INFO_LOG("resizing to {}x{}", width, height);
  DestroyFeature();
  DestroyTextures();

  m_input_texture = g_gpu_device->CreateTexture(width, height, 1, 1, 1, GPUTexture::Type::RenderTarget,
                                                GPUTextureFormat::RGBA16F, GPUTexture::Flags::AllowBindAsImage,
                                                nullptr, 0, error);
  if (!m_input_texture)
  {
    Error::AddPrefix(error, "Failed to create DLSS NR input texture: ");
    return false;
  }
  m_output_texture = g_gpu_device->CreateTexture(width, height, 1, 1, 1, GPUTexture::Type::Texture,
                                                 GPUTextureFormat::RGBA16F, GPUTexture::Flags::AllowBindAsImage,
                                                 nullptr, 0, error);
  if (!m_output_texture)
  {
    Error::AddPrefix(error, "Failed to create DLSS NR output texture: ");
    DestroyTextures();
    return false;
  }

  if (!CreateFeature(error))
  {
    DestroyTextures();
    return false;
  }

  m_width = width;
  m_height = height;
  m_applied_params = m_user_params;
  m_params_dirty = false;
  m_history_reset_needed = true;
  INFO_LOG("input {}x{} RGBA16F", width, height);
  return true;
}

bool DLSSNRProcessor::CreateFeature(Error* error)
{
  D3D12Device& dev = D3D12Device::GetInstance();
  if (dev.InRenderPass())
    dev.EndRenderPass();

  ID3D12GraphicsCommandList4* const cmd = dev.GetCommandList();
  NVSDK_NGX_Parameter* const params = static_cast<NVSDK_NGX_Parameter*>(m_params);
  auto* const in = static_cast<D3D12Texture*>(m_input_texture.get());
  auto* const out = static_cast<D3D12Texture*>(m_output_texture.get());
  const u32 width = in->GetWidth();
  const u32 height = in->GetHeight();

  INFO_LOG("creating feature {} (style={} preset={} intensity={:.1f} tone={:.1f} structure={:.1f} skin={:.1f})",
           DLSS_NR_FEATURE_ID, m_user_params.style, m_user_params.preset, m_user_params.intensity,
           m_user_params.tone, m_user_params.structure, m_user_params.skin_structure);
  params->Set("DLSSNR.Width", width);
  params->Set("DLSSNR.Height", height);
  params->Set("DLSSNR.Enabled", 1);
  params->Set("DLSSNR.Reset", 1);
  params->Set("DLSSNR.Style", m_user_params.style);
  params->Set("DLSSNR.Hint.Render.Preset", m_user_params.preset);
  params->Set("DLSSNR.Intensity", m_user_params.intensity);
  params->Set("DLSSNR.LocalToneStrength", m_user_params.tone);
  params->Set("DLSSNR.LocalStructureStrength", m_user_params.structure);
  params->Set("DLSSNR.SkinStructureStrength", m_user_params.skin_structure);
  params->Set("DLSSNR.UseAutoMask", m_user_params.auto_mask ? 1 : 0);
  params->Set("DLSSNR.UICorrection", 0);
  params->Set("DLSSNR.DepthInverted", 1);
  params->Set("DLSSNR.ScalingRatio", 1.0f);
  params->Set("DLSSNR.MVecScaleX", 1.0f);
  params->Set("DLSSNR.MVecScaleY", 1.0f);
  params->Set("DLSSNR.Color", in->GetResource());
  params->Set("DLSSNR.Output", out->GetResource());
  params->Set("DLSSNR.Backbuffer", out->GetResource());
  params->Set("DLSSNR.ColorSubrectBaseX", 0);
  params->Set("DLSSNR.ColorSubrectBaseY", 0);
  params->Set("DLSSNR.ColorSubrectWidth", width);
  params->Set("DLSSNR.ColorSubrectHeight", height);
  params->Set("DLSSNR.OutputSubrectBaseX", 0);
  params->Set("DLSSNR.OutputSubrectBaseY", 0);
  params->Set("DLSSNR.OutputSubrectWidth", width);
  params->Set("DLSSNR.OutputSubrectHeight", height);

  NVSDK_NGX_Handle* handle = nullptr;
  NVSDK_NGX_Result r;
  if (m_fn_shim_create && m_fn_nr_create_feature)
  {
    r = reinterpret_cast<PFN_ShimCreate>(m_fn_shim_create)(m_fn_nr_create_feature, cmd, DLSS_NR_FEATURE_ID, params,
                                                           &handle);
  }
  else
  {
    WARNING_LOG("caller shim unavailable, calling CreateFeature directly");
    r = reinterpret_cast<PFN_NVSDK_NGX_D3D12_CreateFeature>(m_fn_nr_create_feature)(cmd, DLSS_NR_FEATURE_ID, params,
                                                                                    &handle);
  }
  if (r != NGX_SUCCESS || !handle)
  {
    Error::SetStringFmt(error, "NVSDK_NGX_D3D12_CreateFeature({}) failed: 0x{:08X}", DLSS_NR_FEATURE_ID,
                        static_cast<unsigned>(r));
    ERROR_LOG("CreateFeature failed: 0x{:08X}", static_cast<unsigned>(r));
    return false;
  }

  m_feature = handle;
  dev.SubmitCommandList(false);
  INFO_LOG("feature created");
  return true;
}

void DLSSNRProcessor::DestroyFeature()
{
  if (!m_feature)
    return;

  NVSDK_NGX_Result r = NGX_SUCCESS;
  if (m_fn_shim_release && m_fn_nr_release_feature)
    r = reinterpret_cast<PFN_ShimRelease>(m_fn_shim_release)(m_fn_nr_release_feature,
                                                             static_cast<NVSDK_NGX_Handle*>(m_feature));
  else if (m_fn_release_feature)
    r = reinterpret_cast<PFN_NVSDK_NGX_D3D12_ReleaseFeature>(m_fn_release_feature)(
      static_cast<NVSDK_NGX_Handle*>(m_feature));
  if (r != NGX_SUCCESS)
    ERROR_LOG("ReleaseFeature failed: 0x{:08X}", static_cast<unsigned>(r));

  m_feature = nullptr;
}

void DLSSNRProcessor::DestroyTextures()
{
  if (g_gpu_device)
  {
    g_gpu_device->RecycleTexture(std::move(m_input_texture));
    g_gpu_device->RecycleTexture(std::move(m_output_texture));
  }
  m_input_texture.reset();
  m_output_texture.reset();
  m_width = 0;
  m_height = 0;
}

bool DLSSNRProcessor::Process(GPUTexture* input, GPUTexture* output, bool reset_history, Error* error)
{
  if (!m_params || !m_feature)
  {
    Error::SetStringView(error, "DLSS NR feature is not created.");
    return false;
  }

  D3D12Device& dev = D3D12Device::GetInstance();
  if (dev.InRenderPass())
    dev.EndRenderPass();

  auto* const in = static_cast<D3D12Texture*>(input);
  auto* const out = static_cast<D3D12Texture*>(output);
  ID3D12GraphicsCommandList4* const cmd = dev.GetCommandList();
  NVSDK_NGX_Parameter* const params = static_cast<NVSDK_NGX_Parameter*>(m_params);
  auto* const handle = static_cast<NVSDK_NGX_Handle*>(m_feature);

  in->TransitionToState(cmd, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  out->TransitionToState(cmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  params->Set("DLSSNR.Reset", reset_history ? 1 : 0);

  if (reset_history)
  {
    INFO_LOG("first evaluation");
  }

  NVSDK_NGX_Result r;
  if (m_fn_shim_evaluate && m_fn_nr_evaluate_feature)
  {
    r = reinterpret_cast<PFN_ShimEvaluate>(m_fn_shim_evaluate)(m_fn_nr_evaluate_feature, cmd, handle, params, nullptr);
  }
  else
  {
    r = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(m_fn_nr_evaluate_feature)(cmd, handle, params, nullptr);
  }
  if (r != NGX_SUCCESS)
  {
    ERROR_LOG("EvaluateFeature failed: 0x{:08X}", static_cast<unsigned>(r));
    Error::SetStringFmt(error, "NVSDK_NGX_D3D12_EvaluateFeature() failed: 0x{:08X}", static_cast<unsigned>(r));
    out->TransitionToState(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dev.SubmitCommandList(false);
    return false;
  }

  if (reset_history)
    INFO_LOG("evaluation succeeded");

  const D3D12_RESOURCE_BARRIER uav_barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV,
                                              D3D12_RESOURCE_BARRIER_FLAG_NONE,
                                              {}};
  cmd->ResourceBarrier(1, &uav_barrier);
  out->TransitionToState(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  dev.SubmitCommandList(false);
  return true;
}

bool DLSSNRProcessor::ConsumeHistoryReset()
{
  const bool needed = m_history_reset_needed;
  m_history_reset_needed = false;
  return needed;
}

void DLSSNRProcessor::RequestHistoryReset()
{
  m_history_reset_needed = true;
}

void DLSSNRProcessor::DisableForSession()
{
  if (m_disabled)
    return;

  WARNING_LOG("DLSS NR disabled for this session, falling back to normal rendering");
  Shutdown();
  m_enabled = false;
  m_disabled = true;
}

#else

DLSSNRProcessor& DLSSNRProcessor::GetInstance()
{
  static DLSSNRProcessor s_instance;
  return s_instance;
}

DLSSNRProcessor::DLSSNRProcessor() = default;

DLSSNRProcessor::~DLSSNRProcessor() = default;

void DLSSNRProcessor::SetParameters(const Parameters& params)
{
  m_user_params = params;
}

void DLSSNRProcessor::SetEnabled(bool enabled)
{
  m_enabled = enabled;
}

bool DLSSNRProcessor::Initialize(const std::string& data_path, Error* error)
{
  Error::SetStringView(error, "DLSS NR is only supported on Windows.");
  return false;
}

void DLSSNRProcessor::Shutdown()
{
  m_initialized = false;
}

bool DLSSNRProcessor::Resize(u32 width, u32 height, Error* error)
{
  Error::SetStringView(error, "DLSS NR is only supported on Windows.");
  return false;
}

bool DLSSNRProcessor::Process(GPUTexture* input, GPUTexture* output, bool reset_history, Error* error)
{
  Error::SetStringView(error, "DLSS NR is only supported on Windows.");
  return false;
}

bool DLSSNRProcessor::ConsumeHistoryReset()
{
  return false;
}

void DLSSNRProcessor::RequestHistoryReset() {}

void DLSSNRProcessor::DisableForSession()
{
  m_disabled = true;
}

#endif
