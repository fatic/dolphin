// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Boot/Boot.h"

#ifdef _MSC_VER
#include <filesystem>
namespace fs = std::filesystem;
#define HAS_STD_FILESYSTEM
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Common/Align.h"
#include "Common/CDUtils.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Hash.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"

#include "Core/Boot/DolReader.h"
#include "Core/Boot/ElfReader.h"
#include "Core/CommonTitles.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigManager.h"
#include "Core/FifoPlayer/FifoPlayer.h"
#include "Core/HLE/HLE.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/EXI/EXI_DeviceIPL.h"
#include "Core/HW/Memmap.h"
#include "Core/HW/VideoInterface.h"
#include "Core/Host.h"
#include "Core/IOS/ES/ES.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/IOSC.h"
#include "Core/IOS/Uids.h"
#include "Core/PatchEngine.h"
#include "Core/PowerPC/PPCAnalyst.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"

#include "DiscIO/Enums.h"
#include "DiscIO/GameModDescriptor.h"
#include "DiscIO/RiivolutionParser.h"
#include "DiscIO/RiivolutionPatcher.h"
#include "DiscIO/VolumeDisc.h"
#include "DiscIO/VolumeWad.h"

static std::vector<std::string> ReadM3UFile(const std::string& m3u_path,
                                            const std::string& folder_path)
{
#ifndef HAS_STD_FILESYSTEM
  ASSERT(folder_path.back() == '/');
#endif

  std::vector<std::string> result;
  std::vector<std::string> nonexistent;

  std::ifstream s;
  File::OpenFStream(s, m3u_path, std::ios_base::in);

  std::string line;
  while (std::getline(s, line))
  {
    // This is the UTF-8 representation of U+FEFF.
    constexpr std::string_view utf8_bom = "\xEF\xBB\xBF";

    if (StringBeginsWith(line, utf8_bom))
    {
      WARN_LOG_FMT(BOOT, "UTF-8 BOM in file: {}", m3u_path);
      line.erase(0, utf8_bom.length());
    }

    if (!line.empty() && line.front() != '#')  // Comments start with #
    {
#ifdef HAS_STD_FILESYSTEM
      const std::string path_to_add = PathToString(StringToPath(folder_path) / StringToPath(line));
#else
      const std::string path_to_add = line.front() != '/' ? folder_path + line : line;
#endif

      (File::Exists(path_to_add) ? result : nonexistent).push_back(path_to_add);
    }
  }

  if (!nonexistent.empty())
  {
    PanicAlertFmtT("Files specified in the M3U file \"{0}\" were not found:\n{1}", m3u_path,
                   JoinStrings(nonexistent, "\n"));
    return {};
  }

  if (result.empty())
    PanicAlertFmtT("No paths found in the M3U file \"{0}\"", m3u_path);

  return result;
}

BootSessionData::BootSessionData()
{
}

BootSessionData::BootSessionData(std::optional<std::string> savestate_path,
                                 DeleteSavestateAfterBoot delete_savestate)
    : m_savestate_path(std::move(savestate_path)), m_delete_savestate(delete_savestate)
{
}

BootSessionData::BootSessionData(BootSessionData&& other) = default;

BootSessionData& BootSessionData::operator=(BootSessionData&& other) = default;

BootSessionData::~BootSessionData() = default;

const std::optional<std::string>& BootSessionData::GetSavestatePath() const
{
  return m_savestate_path;
}

DeleteSavestateAfterBoot BootSessionData::GetDeleteSavestate() const
{
  return m_delete_savestate;
}

void BootSessionData::SetSavestateData(std::optional<std::string> savestate_path,
                                       DeleteSavestateAfterBoot delete_savestate)
{
  m_savestate_path = std::move(savestate_path);
  m_delete_savestate = delete_savestate;
}

IOS::HLE::FS::FileSystem* BootSessionData::GetWiiSyncFS() const
{
  return m_wii_sync_fs.get();
}

const std::vector<u64>& BootSessionData::GetWiiSyncTitles() const
{
  return m_wii_sync_titles;
}

const std::string& BootSessionData::GetWiiSyncRedirectFolder() const
{
  return m_wii_sync_redirect_folder;
}

void BootSessionData::InvokeWiiSyncCleanup() const
{
  if (m_wii_sync_cleanup)
    m_wii_sync_cleanup();
}

void BootSessionData::SetWiiSyncData(std::unique_ptr<IOS::HLE::FS::FileSystem> fs,
                                     std::vector<u64> titles, std::string redirect_folder,
                                     WiiSyncCleanupFunction cleanup)
{
  m_wii_sync_fs = std::move(fs);
  m_wii_sync_titles = std::move(titles);
  m_wii_sync_redirect_folder = std::move(redirect_folder);
  m_wii_sync_cleanup = std::move(cleanup);
}

BootParameters::BootParameters(Parameters&& parameters_, BootSessionData boot_session_data_)
    : parameters(std::move(parameters_)), boot_session_data(std::move(boot_session_data_))
{
}

std::unique_ptr<BootParameters> BootParameters::GenerateFromFile(std::string boot_path,
                                                                 BootSessionData boot_session_data_)
{
  return GenerateFromFile(std::vector<std::string>{std::move(boot_path)},
                          std::move(boot_session_data_));
}

std::unique_ptr<BootParameters> BootParameters::GenerateFromFile(std::vector<std::string> paths,
                                                                 BootSessionData boot_session_data_)
{
  ASSERT(!paths.empty());

  const bool is_drive = Common::IsCDROMDevice(paths.front());
  // Check if the file exist, we may have gotten it from a --elf command line
  // that gave an incorrect file name
  if (!is_drive && !File::Exists(paths.front()))
  {
    PanicAlertFmtT("The specified file \"{0}\" does not exist", paths.front());
    return {};
  }

  std::string folder_path;
  std::string extension;
  SplitPath(paths.front(), &folder_path, nullptr, &extension);
  Common::ToLower(&extension);

  if (extension == ".m3u" || extension == ".m3u8")
  {
    paths = ReadM3UFile(paths.front(), folder_path);
    if (paths.empty())
      return {};

    SplitPath(paths.front(), nullptr, nullptr, &extension);
    Common::ToLower(&extension);
  }

  std::string path = paths.front();
  if (paths.size() == 1)
    paths.clear();

#ifdef ANDROID
  if (extension.empty() && IsPathAndroidContent(path))
  {
    const std::string display_name = GetAndroidContentDisplayName(path);
    SplitPath(display_name, nullptr, nullptr, &extension);
    Common::ToLower(&extension);
  }
#endif

  static const std::unordered_set<std::string> disc_image_extensions = {
      {".gcm", ".iso", ".tgc", ".wbfs", ".ciso", ".gcz", ".wia", ".rvz", ".dol", ".elf"}};
  if (disc_image_extensions.find(extension) != disc_image_extensions.end() || is_drive)
  {
    std::unique_ptr<DiscIO::VolumeDisc> disc = DiscIO::CreateDisc(path);
    if (disc)
    {
      return std::make_unique<BootParameters>(Disc{std::move(path), std::move(disc), paths},
                                              std::move(boot_session_data_));
    }

    if (extension == ".elf")
    {
      auto elf_reader = std::make_unique<ElfReader>(path);
      return std::make_unique<BootParameters>(Executable{std::move(path), std::move(elf_reader)},
                                              std::move(boot_session_data_));
    }

    if (extension == ".dol")
    {
      auto dol_reader = std::make_unique<DolReader>(path);
      return std::make_unique<BootParameters>(Executable{std::move(path), std::move(dol_reader)},
                                              std::move(boot_session_data_));
    }

    if (is_drive)
    {
      PanicAlertFmtT("Could not read \"{0}\". "
                     "There is no disc in the drive or it is not a GameCube/Wii backup. "
                     "Please note that Dolphin cannot play games directly from the original "
                     "GameCube and Wii discs.",
                     path);
    }
    else
    {
      PanicAlertFmtT("\"{0}\" is an invalid GCM/ISO file, or is not a GC/Wii ISO.", path);
    }
    return {};
  }

  if (extension == ".dff")
    return std::make_unique<BootParameters>(DFF{std::move(path)}, std::move(boot_session_data_));

  if (extension == ".wad")
  {
    std::unique_ptr<DiscIO::VolumeWAD> wad = DiscIO::CreateWAD(std::move(path));
    if (wad)
      return std::make_unique<BootParameters>(std::move(*wad), std::move(boot_session_data_));
  }

  if (extension == ".json")
  {
    auto descriptor = DiscIO::ParseGameModDescriptorFile(path);
    if (descriptor)
    {
      auto boot_params = GenerateFromFile(descriptor->base_file, std::move(boot_session_data_));
      if (!boot_params)
      {
        PanicAlertFmtT("Could not recognize file {0}", descriptor->base_file);
        return nullptr;
      }

      if (descriptor->riivolution && std::holds_alternative<Disc>(boot_params->parameters))
      {
        const auto& volume = *std::get<Disc>(boot_params->parameters).volume;
        AddRiivolutionPatches(boot_params.get(),
                              DiscIO::Riivolution::GenerateRiivolutionPatchesFromGameModDescriptor(
                                  *descriptor->riivolution, volume.GetGameID(),
                                  volume.GetRevision(), volume.GetDiscNumber()));
      }

      return boot_params;
    }
  }

  PanicAlertFmtT("Could not recognize file {0}", path);
  return {};
}

BootParameters::IPL::IPL(DiscIO::Region region_) : region(region_)
{
  const std::string directory = SConfig::GetInstance().GetDirectoryForRegion(region);
  path = SConfig::GetInstance().GetBootROMPath(directory);
}

BootParameters::IPL::IPL(DiscIO::Region region_, Disc&& disc_) : IPL(region_)
{
  disc = std::move(disc_);
}

// Inserts a disc into the emulated disc drive and returns a pointer to it.
// The returned pointer must only be used while we are still booting,
// because DVDThread can do whatever it wants to the disc after that.
static const DiscIO::VolumeDisc* SetDisc(std::unique_ptr<DiscIO::VolumeDisc> disc,
                                         std::vector<std::string> auto_disc_change_paths = {})
{
  const DiscIO::VolumeDisc* pointer = disc.get();
  DVDInterface::SetDisc(std::move(disc), auto_disc_change_paths);
  return pointer;
}

bool CBoot::DVDRead(const DiscIO::VolumeDisc& disc, u64 dvd_offset, u32 output_address, u32 length,
                    const DiscIO::Partition& partition)
{
  std::vector<u8> buffer(length);
  if (!disc.Read(dvd_offset, length, buffer.data(), partition))
    return false;
  Memory::CopyToEmu(output_address, buffer.data(), length);
  return true;
}

bool CBoot::DVDReadDiscID(const DiscIO::VolumeDisc& disc, u32 output_address)
{
  std::array<u8, 0x20> buffer;
  if (!disc.Read(0, buffer.size(), buffer.data(), DiscIO::PARTITION_NONE))
    return false;
  Memory::CopyToEmu(output_address, buffer.data(), buffer.size());
  // Transition out of the DiscIdNotRead state (which the drive should be in at this point,
  // on the assumption that this is only used for the first read)
  DVDInterface::SetDriveState(DVDInterface::DriveState::ReadyNoReadsMade);
  return true;
}

void CBoot::UpdateDebugger_MapLoaded()
{
  Host_NotifyMapLoaded();
}

// Get map file paths for the active title.
bool CBoot::FindMapFile(std::string* existing_map_file, std::string* writable_map_file)
{
  const std::string& game_id = SConfig::GetInstance().m_debugger_game_id;

  if (writable_map_file)
    *writable_map_file = File::GetUserPath(D_MAPS_IDX) + game_id + ".map";

  static const std::array<std::string, 2> maps_directories{
      File::GetUserPath(D_MAPS_IDX),
      File::GetSysDirectory() + MAPS_DIR DIR_SEP,
  };
  for (const auto& directory : maps_directories)
  {
    std::string path = directory + game_id + ".map";
    if (File::Exists(path))
    {
      if (existing_map_file)
        *existing_map_file = std::move(path);

      return true;
    }
  }

  return false;
}

bool CBoot::LoadMapFromFilename()
{
  std::string strMapFilename;
  bool found = FindMapFile(&strMapFilename, nullptr);
  if (found && g_symbolDB.LoadMap(strMapFilename))
  {
    UpdateDebugger_MapLoaded();
    return true;
  }

  return false;
}

// If ipl.bin is not found, this function does *some* of what BS1 does:
// loading IPL(BS2) and jumping to it.
// It does not initialize the hardware or anything else like BS1 does.
bool CBoot::Load_BS2(const std::string& boot_rom_filename)
{
  // CRC32 hashes of the IPL file, obtained from Redump
  constexpr u32 NTSC_v1_0 = 0x6DAC1F2A;
  constexpr u32 NTSC_v1_1 = 0xD5E6FEEA;
  constexpr u32 NTSC_v1_2 = 0x86573808;
  constexpr u32 MPAL_v1_1 = 0x667D0B64;  // Brazil
  constexpr u32 PAL_v1_0 = 0x4F319F43;
  constexpr u32 PAL_v1_2 = 0xAD1B7F16;

  // Load the whole ROM dump
  std::string data;
  if (!File::ReadFileToString(boot_rom_filename, data))
    return false;

  const u32 ipl_hash = Common::ComputeCRC32(data);
  bool known_ipl = false;
  bool pal_ipl = false;
  switch (ipl_hash)
  {
  case NTSC_v1_0:
  case NTSC_v1_1:
  case NTSC_v1_2:
  case MPAL_v1_1:
    known_ipl = true;
    break;
  case PAL_v1_0:
  case PAL_v1_2:
    pal_ipl = true;
    known_ipl = true;
    break;
  default:
    PanicAlertFmtT("The IPL file is not a known good dump. (CRC32: {0:x})", ipl_hash);
    break;
  }

  const DiscIO::Region boot_region = SConfig::GetInstance().m_region;
  if (known_ipl && pal_ipl != (boot_region == DiscIO::Region::PAL))
  {
    PanicAlertFmtT("{0} IPL found in {1} directory. The disc might not be recognized",
                   pal_ipl ? "PAL" : "NTSC", SConfig::GetDirectoryForRegion(boot_region));
  }

  // Run the descrambler over the encrypted section containing BS1/BS2
  ExpansionInterface::CEXIIPL::Descrambler((u8*)data.data() + 0x100, 0x1AFE00);

  // TODO: Execution is supposed to start at 0xFFF00000, not 0x81200000;
  // copying the initial boot code to 0x81200000 is a hack.
  // For now, HLE the first few instructions and start at 0x81200150
  // to work around this.
  Memory::CopyToEmu(0x01200000, data.data() + 0x100, 0x700);
  Memory::CopyToEmu(0x01300000, data.data() + 0x820, 0x1AFE00);

  PowerPC::ppcState.gpr[3] = 0xfff0001f;
  PowerPC::ppcState.gpr[4] = 0x00002030;
  PowerPC::ppcState.gpr[5] = 0x0000009c;

  MSR.FP = 1;
  MSR.DR = 1;
  MSR.IR = 1;

  PowerPC::ppcState.spr[SPR_HID0] = 0x0011c464;
  PowerPC::ppcState.spr[SPR_IBAT3U] = 0xfff0001f;
  PowerPC::ppcState.spr[SPR_IBAT3L] = 0xfff00001;
  PowerPC::ppcState.spr[SPR_DBAT3U] = 0xfff0001f;
  PowerPC::ppcState.spr[SPR_DBAT3L] = 0xfff00001;
  SetupBAT(/*is_wii*/ false);

  PC = 0x81200150;
  return true;
}

static void SetDefaultDisc()
{
  const std::string default_iso = Config::Get(Config::MAIN_DEFAULT_ISO);
  if (!default_iso.empty())
    SetDisc(DiscIO::CreateDisc(default_iso));
}

static void CopyDefaultExceptionHandlers()
{
  constexpr u32 EXCEPTION_HANDLER_ADDRESSES[] = {0x00000100, 0x00000200, 0x00000300, 0x00000400,
                                                 0x00000500, 0x00000600, 0x00000700, 0x00000800,
                                                 0x00000900, 0x00000C00, 0x00000D00, 0x00000F00,
                                                 0x00001300, 0x00001400, 0x00001700};

  constexpr u32 RFI_INSTRUCTION = 0x4C000064;
  for (const u32 address : EXCEPTION_HANDLER_ADDRESSES)
    Memory::Write_U32(RFI_INSTRUCTION, address);
}

// Third boot step after BootManager and Core. See Call schedule in BootManager.cpp
bool CBoot::BootUp(std::unique_ptr<BootParameters> boot)
{
  SConfig& config = SConfig::GetInstance();

  if (!g_symbolDB.IsEmpty())
  {
    g_symbolDB.Clear();
    UpdateDebugger_MapLoaded();
  }

  // PAL Wii uses NTSC framerate and linecount in 60Hz modes
  VideoInterface::Preset(DiscIO::IsNTSC(config.m_region) ||
                         (config.bWii && Config::Get(Config::SYSCONF_PAL60)));

  struct BootTitle
  {
    BootTitle(const std::vector<DiscIO::Riivolution::Patch>& patches)
        : config(SConfig::GetInstance()), riivolution_patches(patches)
    {
    }
    bool operator()(BootParameters::Disc& disc) const
    {
      NOTICE_LOG_FMT(BOOT, "Booting from disc: {}", disc.path);
      const DiscIO::VolumeDisc* volume =
          SetDisc(std::move(disc.volume), disc.auto_disc_change_paths);

      if (!volume)
        return false;

      if (!EmulatedBS2(config.bWii, *volume, riivolution_patches))
        return false;

      SConfig::OnNewTitleLoad();
      return true;
    }

    bool operator()(const BootParameters::Executable& executable) const
    {
      NOTICE_LOG_FMT(BOOT, "Booting from executable: {}", executable.path);

      if (!executable.reader->IsValid())
        return false;

      if (!executable.reader->LoadIntoMemory())
      {
        PanicAlertFmtT("Failed to load the executable to memory.");
        return false;
      }

      SetDefaultDisc();

      SetupMSR();
      SetupBAT(config.bWii);
      CopyDefaultExceptionHandlers();

      if (config.bWii)
      {
        PowerPC::ppcState.spr[SPR_HID0] = 0x0011c464;
        PowerPC::ppcState.spr[SPR_HID4] = 0x82000000;

        // Set a value for the SP. It doesn't matter where this points to,
        // as long as it is a valid location. This value is taken from a homebrew binary.
        PowerPC::ppcState.gpr[1] = 0x8004d4bc;

        // Because there is no TMD to get the requested system (IOS) version from,
        // we default to IOS58, which is the version used by the Homebrew Channel.
        SetupWiiMemory(IOS::HLE::IOSC::ConsoleType::Retail);
        IOS::HLE::GetIOS()->BootIOS(Titles::IOS(58));
      }
      else
      {
        SetupGCMemory();
      }

      SConfig::OnNewTitleLoad();

      PC = executable.reader->GetEntryPoint();

      if (executable.reader->LoadSymbols())
      {
        UpdateDebugger_MapLoaded();
        HLE::PatchFunctions();
      }
      return true;
    }

    bool operator()(const DiscIO::VolumeWAD& wad) const
    {
      SetDefaultDisc();
      if (!Boot_WiiWAD(wad))
        return false;

      SConfig::OnNewTitleLoad();
      return true;
    }

    bool operator()(const BootParameters::NANDTitle& nand_title) const
    {
      SetDefaultDisc();
      if (!BootNANDTitle(nand_title.id))
        return false;

      SConfig::OnNewTitleLoad();
      return true;
    }

    bool operator()(const BootParameters::IPL& ipl) const
    {
      NOTICE_LOG_FMT(BOOT, "Booting GC IPL: {}", ipl.path);
      if (!File::Exists(ipl.path))
      {
        if (ipl.disc)
          PanicAlertFmtT("Cannot start the game, because the GC IPL could not be found.");
        else
          PanicAlertFmtT("Cannot find the GC IPL.");
        return false;
      }

      if (!Load_BS2(ipl.path))
        return false;

      if (ipl.disc)
      {
        NOTICE_LOG_FMT(BOOT, "Inserting disc: {}", ipl.disc->path);
        SetDisc(DiscIO::CreateDisc(ipl.disc->path), ipl.disc->auto_disc_change_paths);
      }

      SConfig::OnNewTitleLoad();
      return true;
    }

    bool operator()(const BootParameters::DFF& dff) const
    {
      NOTICE_LOG_FMT(BOOT, "Booting DFF: {}", dff.dff_path);
      return FifoPlayer::GetInstance().Open(dff.dff_path);
    }

  private:
    const SConfig& config;
    const std::vector<DiscIO::Riivolution::Patch>& riivolution_patches;
  };

  if (!std::visit(BootTitle(boot->riivolution_patches), boot->parameters))
    return false;

  DiscIO::Riivolution::ApplyGeneralMemoryPatches(boot->riivolution_patches);

  return true;
}

BootExecutableReader::BootExecutableReader(const std::string& file_name)
    : BootExecutableReader(File::IOFile{file_name, "rb"})
{
}

BootExecutableReader::BootExecutableReader(File::IOFile file)
{
  file.Seek(0, SEEK_SET);
  m_bytes.resize(file.GetSize());
  file.ReadBytes(m_bytes.data(), m_bytes.size());
}

BootExecutableReader::BootExecutableReader(std::vector<u8> bytes) : m_bytes(std::move(bytes))
{
}

BootExecutableReader::~BootExecutableReader() = default;

void StateFlags::UpdateChecksum()
{
  constexpr size_t length_in_bytes = sizeof(StateFlags) - 4;
  constexpr size_t num_elements = length_in_bytes / sizeof(u32);
  std::array<u32, num_elements> flag_data;
  std::memcpy(flag_data.data(), &flags, length_in_bytes);
  checksum = std::accumulate(flag_data.cbegin(), flag_data.cend(), 0U);
}

void UpdateStateFlags(std::function<void(StateFlags*)> update_function)
{
  CreateSystemMenuTitleDirs();
  const std::string file_path = Common::GetTitleDataPath(Titles::SYSTEM_MENU) + "/" WII_STATE;
  const auto fs = IOS::HLE::GetIOS()->GetFS();
  constexpr IOS::HLE::FS::Mode rw_mode = IOS::HLE::FS::Mode::ReadWrite;
  const auto file = fs->CreateAndOpenFile(IOS::SYSMENU_UID, IOS::SYSMENU_GID, file_path,
                                          {rw_mode, rw_mode, rw_mode});
  if (!file)
    return;

  StateFlags state{};
  if (file->GetStatus()->size == sizeof(StateFlags))
    file->Read(&state, 1);

  update_function(&state);
  state.UpdateChecksum();

  file->Seek(0, IOS::HLE::FS::SeekMode::Set);
  file->Write(&state, 1);
}

void CreateSystemMenuTitleDirs()
{
  const auto es = IOS::HLE::GetIOS()->GetES();
  es->CreateTitleDirectories(Titles::SYSTEM_MENU, IOS::SYSMENU_GID);
}

void AddRiivolutionPatches(BootParameters* boot_params,
                           std::vector<DiscIO::Riivolution::Patch> riivolution_patches)
{
  if (riivolution_patches.empty())
    return;
  if (!std::holds_alternative<BootParameters::Disc>(boot_params->parameters))
    return;

  auto& disc = std::get<BootParameters::Disc>(boot_params->parameters);
  disc.volume = DiscIO::CreateDisc(DiscIO::DirectoryBlobReader::Create(
      std::move(disc.volume),
      [&](std::vector<DiscIO::FSTBuilderNode>* fst, DiscIO::FSTBuilderNode* dol_node) {
        DiscIO::Riivolution::ApplyPatchesToFiles(riivolution_patches, fst, dol_node);
      }));
  boot_params->riivolution_patches = std::move(riivolution_patches);
}
