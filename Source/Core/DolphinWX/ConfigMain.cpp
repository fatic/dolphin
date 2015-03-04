// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filepicker.h>
#include <wx/gbsizer.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinbutt.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include "AudioCommon/AudioCommon.h"

#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileSearch.h"
#include "Common/SysConf.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HotkeyManager.h"
#include "Core/Movie.h"
#include "Core/NetPlayProto.h"
#include "Core/HW/EXI.h"
#include "Core/HW/GCMemcard.h"
#include "Core/HW/SI.h"
#include "Core/HW/DSPHLE/DSPHLE.h"
#include "Core/HW/DSPLLE/DSPLLE.h"
#include "Core/IPC_HLE/WII_IPC_HLE.h"
#include "Core/PowerPC/PowerPC.h"

#include "DiscIO/NANDContentLoader.h"

#include "DolphinWX/ConfigMain.h"
#include "DolphinWX/Frame.h"
#include "DolphinWX/Globals.h"
#include "DolphinWX/HotkeyDlg.h"
#include "DolphinWX/InputConfigDiag.h"
#include "DolphinWX/Main.h"
#include "DolphinWX/WxUtils.h"
#include "DolphinWX/Debugger/CodeWindow.h"

#include "InputCommon/InputConfig.h"

#include "VideoCommon/VideoBackendBase.h"

#define TEXT_BOX(page, text) new wxStaticText(page, wxID_ANY, text)

struct CPUCore
{
	int CPUid;
	const char *name;
};
const CPUCore CPUCores[] = {
	{0, wxTRANSLATE("Interpreter (VERY slow)")},
#ifdef _M_X86_64
	{1, wxTRANSLATE("JIT Recompiler (recommended)")},
	{2, wxTRANSLATE("JITIL Recompiler (slower, experimental)")},
#elif defined(_M_ARM_32)
	{3, wxTRANSLATE("Arm JIT (experimental)")},
#elif defined(_M_ARM_64)
	{4, wxTRANSLATE("Arm64 JIT (experimental)")},
#endif
};

// keep these in sync with CConfigMain::InitializeGUILists
static const wxLanguage langIds[] =
{
	wxLANGUAGE_DEFAULT,

	wxLANGUAGE_CATALAN,
	wxLANGUAGE_CZECH,
	wxLANGUAGE_GERMAN,
	wxLANGUAGE_ENGLISH,
	wxLANGUAGE_SPANISH,
	wxLANGUAGE_FRENCH,
	wxLANGUAGE_ITALIAN,
	wxLANGUAGE_HUNGARIAN,
	wxLANGUAGE_DUTCH,
	wxLANGUAGE_NORWEGIAN_BOKMAL,
	wxLANGUAGE_POLISH,
	wxLANGUAGE_PORTUGUESE,
	wxLANGUAGE_PORTUGUESE_BRAZILIAN,
	wxLANGUAGE_SERBIAN,
	wxLANGUAGE_SWEDISH,
	wxLANGUAGE_TURKISH,

	wxLANGUAGE_GREEK,
	wxLANGUAGE_RUSSIAN,
	wxLANGUAGE_HEBREW,
	wxLANGUAGE_ARABIC,
	wxLANGUAGE_FARSI,
	wxLANGUAGE_KOREAN,
	wxLANGUAGE_JAPANESE,
	wxLANGUAGE_CHINESE_SIMPLIFIED,
	wxLANGUAGE_CHINESE_TRADITIONAL,
};

// Strings for Device Selections
#define DEV_NONE_STR        _trans("<Nothing>")
#define DEV_DUMMY_STR       _trans("Dummy")

#define EXIDEV_MEMCARD_STR  _trans("Memory Card")
#define EXIDEV_MEMDIR_STR   _trans("GCI Folder")
#define EXIDEV_MIC_STR      _trans("Mic")
#define EXIDEV_BBA_STR      "BBA"
#define EXIDEV_AGP_STR      "Advance Game Port"
#define EXIDEV_AM_BB_STR    _trans("AM-Baseboard")
#define EXIDEV_GECKO_STR    "USBGecko"

#ifdef WIN32
//only used with xgettext to be picked up as translatable string.
//win32 does not have wx on its path, the provided wxALL_FILES
//translation does not work there.
#define unusedALL_FILES wxTRANSLATE("All files (*.*)|*.*");
#endif

BEGIN_EVENT_TABLE(CConfigMain, wxDialog)

EVT_CLOSE(CConfigMain::OnClose)
EVT_BUTTON(wxID_OK, CConfigMain::OnOk)


EVT_CHECKBOX(ID_CPUTHREAD, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_IDLESKIP, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_ENABLECHEATS, CConfigMain::CoreSettingsChanged)
EVT_CHOICE(ID_FRAMELIMIT, CConfigMain::CoreSettingsChanged)

EVT_RADIOBOX(ID_CPUENGINE, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_NTSCJ, CConfigMain::CoreSettingsChanged)
EVT_SLIDER(ID_OVERCLOCK, CConfigMain::CoreSettingsChanged)
EVT_CHECKBOX(ID_ENABLEOVERCLOCK, CConfigMain::CoreSettingsChanged)

EVT_RADIOBOX(ID_DSPENGINE, CConfigMain::AudioSettingsChanged)
EVT_CHECKBOX(ID_ENABLE_THROTTLE, CConfigMain::AudioSettingsChanged)
EVT_CHECKBOX(ID_DPL2DECODER, CConfigMain::AudioSettingsChanged)
EVT_CHOICE(ID_BACKEND, CConfigMain::AudioSettingsChanged)
EVT_SLIDER(ID_VOLUME, CConfigMain::AudioSettingsChanged)

EVT_CHECKBOX(ID_INTERFACE_CONFIRMSTOP, CConfigMain::DisplaySettingsChanged)
EVT_CHECKBOX(ID_INTERFACE_USEPANICHANDLERS, CConfigMain::DisplaySettingsChanged)
EVT_CHECKBOX(ID_INTERFACE_ONSCREENDISPLAYMESSAGES, CConfigMain::DisplaySettingsChanged)
EVT_CHOICE(ID_INTERFACE_LANG, CConfigMain::DisplaySettingsChanged)
EVT_BUTTON(ID_HOTKEY_CONFIG, CConfigMain::DisplaySettingsChanged)


EVT_CHOICE(ID_GC_SRAM_LNG, CConfigMain::GCSettingsChanged)
EVT_CHECKBOX(ID_GC_ALWAYS_HLE_BS2, CConfigMain::GCSettingsChanged)

EVT_CHOICE(ID_GC_EXIDEVICE_SLOTA, CConfigMain::GCSettingsChanged)
EVT_BUTTON(ID_GC_EXIDEVICE_SLOTA_PATH, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_EXIDEVICE_SLOTB, CConfigMain::GCSettingsChanged)
EVT_BUTTON(ID_GC_EXIDEVICE_SLOTB_PATH, CConfigMain::GCSettingsChanged)
EVT_CHOICE(ID_GC_EXIDEVICE_SP1, CConfigMain::GCSettingsChanged)

EVT_CHECKBOX(ID_WII_IPL_SSV, CConfigMain::WiiSettingsChanged)
EVT_CHECKBOX(ID_WII_IPL_E60, CConfigMain::WiiSettingsChanged)
EVT_CHOICE(ID_WII_IPL_AR, CConfigMain::WiiSettingsChanged)
EVT_CHOICE(ID_WII_IPL_LNG, CConfigMain::WiiSettingsChanged)

EVT_CHECKBOX(ID_WII_SD_CARD, CConfigMain::WiiSettingsChanged)
EVT_CHECKBOX(ID_WII_KEYBOARD, CConfigMain::WiiSettingsChanged)


EVT_LISTBOX(ID_ISOPATHS, CConfigMain::ISOPathsSelectionChanged)
EVT_CHECKBOX(ID_RECURSIVEISOPATH, CConfigMain::RecursiveDirectoryChanged)
EVT_BUTTON(ID_ADDISOPATH, CConfigMain::AddRemoveISOPaths)
EVT_BUTTON(ID_REMOVEISOPATH, CConfigMain::AddRemoveISOPaths)

EVT_FILEPICKER_CHANGED(ID_DEFAULTISO, CConfigMain::DefaultISOChanged)
EVT_DIRPICKER_CHANGED(ID_DVDROOT, CConfigMain::DVDRootChanged)
EVT_FILEPICKER_CHANGED(ID_APPLOADERPATH, CConfigMain::ApploaderPathChanged)
EVT_DIRPICKER_CHANGED(ID_NANDROOT, CConfigMain::NANDRootChanged)


END_EVENT_TABLE()

CConfigMain::CConfigMain(wxWindow* parent, wxWindowID id, const wxString& title,
		const wxPoint& position, const wxSize& size, long style)
	: wxDialog(parent, id, title, position, size, style)
{
	// Control refreshing of the ISOs list
	bRefreshList = false;

	CreateGUIControls();

	// Update selected ISO paths
	for (const std::string& folder : SConfig::GetInstance().m_ISOFolder)
	{
		ISOPaths->Append(StrToWxStr(folder));
	}
}

CConfigMain::~CConfigMain()
{
}

void CConfigMain::SetSelectedTab(int tab)
{
	// TODO : this is just a quick and dirty way to do it, possible cleanup

	switch (tab)
	{
	case ID_AUDIOPAGE:
		Notebook->SetSelection(2);
		break;
	}
}

// Used to restrict changing of some options while emulator is running
void CConfigMain::UpdateGUI()
{
	if (Core::IsRunning())
	{
		// Disable the Core stuff on GeneralPage
		CPUThread->Disable();
		SkipIdle->Disable();
		EnableCheats->Disable();

		CPUEngine->Disable();
		_NTSCJ->Disable();

		// Disable stuff on AudioPage
		DSPEngine->Disable();
		DPL2Decoder->Disable();
		Latency->Disable();

		// Disable stuff on GamecubePage
		GCSystemLang->Disable();
		GCAlwaysHLE_BS2->Disable();

		// Disable stuff on WiiPage
		WiiScreenSaver->Disable();
		WiiPAL60->Disable();
		WiiAspectRatio->Disable();
		WiiSystemLang->Disable();

		// Disable stuff on PathsPage
		PathsPage->Disable();
	}
}
void CConfigMain::InitializeGUILists()
{
	// General page
	// Framelimit
	arrayStringFor_Framelimit.Add(_("Off"));
	arrayStringFor_Framelimit.Add(_("Auto"));
	for (int i = 5; i <= 120; i += 5) // from 5 to 120
		arrayStringFor_Framelimit.Add(wxString::Format("%i", i));

	// Emulator Engine
	for (const CPUCore& CPUCores_a : CPUCores)
		arrayStringFor_CPUEngine.Add(wxGetTranslation(CPUCores_a.name));

	// DSP Engine
	arrayStringFor_DSPEngine.Add(_("DSP HLE emulation (fast)"));
	arrayStringFor_DSPEngine.Add(_("DSP LLE recompiler"));
	arrayStringFor_DSPEngine.Add(_("DSP LLE interpreter (slow)"));

	// GameCube page
	// GC Language arrayStrings
	arrayStringFor_GCSystemLang.Add(_("English"));
	arrayStringFor_GCSystemLang.Add(_("German"));
	arrayStringFor_GCSystemLang.Add(_("French"));
	arrayStringFor_GCSystemLang.Add(_("Spanish"));
	arrayStringFor_GCSystemLang.Add(_("Italian"));
	arrayStringFor_GCSystemLang.Add(_("Dutch"));


	// Wii page
	// Sensorbar Position
	arrayStringFor_WiiSensBarPos.Add(_("Bottom"));
	arrayStringFor_WiiSensBarPos.Add(_("Top"));

	// Aspect ratio
	arrayStringFor_WiiAspectRatio.Add("4:3");
	arrayStringFor_WiiAspectRatio.Add("16:9");

	// Wii Language arrayStrings
	arrayStringFor_WiiSystemLang = arrayStringFor_GCSystemLang;
	arrayStringFor_WiiSystemLang.Insert(_("Japanese"), 0);
	arrayStringFor_WiiSystemLang.Add(_("Simplified Chinese"));
	arrayStringFor_WiiSystemLang.Add(_("Traditional Chinese"));
	arrayStringFor_WiiSystemLang.Add(_("Korean"));

	// GUI language arrayStrings
	// keep these in sync with the langIds array at the beginning of this file
	arrayStringFor_InterfaceLang.Add(_("<System Language>"));

	arrayStringFor_InterfaceLang.Add(L"Catal\u00E0");                                       // Catalan
	arrayStringFor_InterfaceLang.Add(L"\u010Ce\u0161tina");                                 // Czech
	arrayStringFor_InterfaceLang.Add(L"Deutsch");                                           // German
	arrayStringFor_InterfaceLang.Add(L"English");                                           // English
	arrayStringFor_InterfaceLang.Add(L"Espa\u00F1ol");                                      // Spanish
	arrayStringFor_InterfaceLang.Add(L"Fran\u00E7ais");                                     // French
	arrayStringFor_InterfaceLang.Add(L"Italiano");                                          // Italian
	arrayStringFor_InterfaceLang.Add(L"Magyar");                                            // Hungarian
	arrayStringFor_InterfaceLang.Add(L"Nederlands");                                        // Dutch
	arrayStringFor_InterfaceLang.Add(L"Norsk bokm\u00E5l");                                 // Norwegian
	arrayStringFor_InterfaceLang.Add(L"Polski");                                            // Polish
	arrayStringFor_InterfaceLang.Add(L"Portugu\u00EAs");                                    // Portuguese
	arrayStringFor_InterfaceLang.Add(L"Portugu\u00EAs (Brasil)");                           // Portuguese (Brazil)
	arrayStringFor_InterfaceLang.Add(L"Srpski");                                            // Serbian
	arrayStringFor_InterfaceLang.Add(L"Svenska");                                           // Swedish
	arrayStringFor_InterfaceLang.Add(L"T\u00FCrk\u00E7e");                                  // Turkish

	arrayStringFor_InterfaceLang.Add(L"\u0395\u03BB\u03BB\u03B7\u03BD\u03B9\u03BA\u03AC");  // Greek
	arrayStringFor_InterfaceLang.Add(L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439");        // Russian
	arrayStringFor_InterfaceLang.Add(L"\u05E2\u05D1\u05E8\u05D9\u05EA");                    // Hebrew
	arrayStringFor_InterfaceLang.Add(L"\u0627\u0644\u0639\u0631\u0628\u064A\u0629");        // Arabic
	arrayStringFor_InterfaceLang.Add(L"\u0641\u0627\u0631\u0633\u06CC");                    // Farsi
	arrayStringFor_InterfaceLang.Add(L"\uD55C\uAD6D\uC5B4");                                // Korean
	arrayStringFor_InterfaceLang.Add(L"\u65E5\u672C\u8A9E");                                // Japanese
	arrayStringFor_InterfaceLang.Add(L"\u7B80\u4F53\u4E2D\u6587");                          // Simplified Chinese
	arrayStringFor_InterfaceLang.Add(L"\u7E41\u9AD4\u4E2D\u6587");                          // Traditional Chinese
}

void CConfigMain::InitializeGUIValues()
{
	const SCoreStartupParameter& startup_params = SConfig::GetInstance().m_LocalCoreStartupParameter;

	// General - Basic
	CPUThread->SetValue(startup_params.bCPUThread);
	SkipIdle->SetValue(startup_params.bSkipIdle);
	EnableCheats->SetValue(startup_params.bEnableCheats);
	Framelimit->SetSelection(SConfig::GetInstance().m_Framelimit);
	int ocFactor = (int)(log2f(SConfig::GetInstance().m_OCFactor) * 25.f + 100.f + 0.5f);
	EnableOC->SetValue(SConfig::GetInstance().m_OCEnable);
	OCSlider->SetValue(ocFactor);
	UpdateCPUClock();

	// General - Advanced
	for (unsigned int a = 0; a < (sizeof(CPUCores) / sizeof(CPUCore)); ++a)
		if (CPUCores[a].CPUid == startup_params.iCPUCore)
			CPUEngine->SetSelection(a);
	_NTSCJ->SetValue(startup_params.bForceNTSCJ);


	// Display - Interface
	ConfirmStop->SetValue(startup_params.bConfirmStop);
	UsePanicHandlers->SetValue(startup_params.bUsePanicHandlers);
	OnScreenDisplayMessages->SetValue(startup_params.bOnScreenDisplayMessages);
	// need redesign
	for (unsigned int i = 0; i < sizeof(langIds) / sizeof(wxLanguage); i++)
	{
		if (langIds[i] == SConfig::GetInstance().m_InterfaceLanguage)
		{
			InterfaceLang->SetSelection(i);
			break;
		}
	}

	// Audio DSP Engine
	if (startup_params.bDSPHLE)
		DSPEngine->SetSelection(0);
	else
		DSPEngine->SetSelection(SConfig::GetInstance().m_DSPEnableJIT ? 1 : 2);

	// Audio
	VolumeSlider->Enable(SupportsVolumeChanges(SConfig::GetInstance().sBackend));
	VolumeSlider->SetValue(SConfig::GetInstance().m_Volume);
	VolumeText->SetLabel(wxString::Format("%d %%", SConfig::GetInstance().m_Volume));
	DPL2Decoder->Enable(std::string(SConfig::GetInstance().sBackend) == BACKEND_OPENAL
			|| std::string(SConfig::GetInstance().sBackend) == BACKEND_PULSEAUDIO);
	DPL2Decoder->SetValue(startup_params.bDPL2Decoder);
	Latency->Enable(std::string(SConfig::GetInstance().sBackend) == BACKEND_OPENAL);
	Latency->SetValue(startup_params.iLatency);
	// add backends to the list
	AddAudioBackends();


	// GameCube - IPL
	GCSystemLang->SetSelection(startup_params.SelectedLanguage);
	GCAlwaysHLE_BS2->SetValue(startup_params.bHLE_BS2);

	// GameCube - Devices
	wxArrayString SlotDevices;
		SlotDevices.Add(_(DEV_NONE_STR));
		SlotDevices.Add(_(DEV_DUMMY_STR));
		SlotDevices.Add(_(EXIDEV_MEMCARD_STR));
		SlotDevices.Add(_(EXIDEV_MEMDIR_STR));
		SlotDevices.Add(_(EXIDEV_GECKO_STR));
		SlotDevices.Add(_(EXIDEV_AGP_STR));

#if HAVE_PORTAUDIO
		SlotDevices.Add(_(EXIDEV_MIC_STR));
#endif

	wxArrayString SP1Devices;
		SP1Devices.Add(_(DEV_NONE_STR));
		SP1Devices.Add(_(DEV_DUMMY_STR));
		SP1Devices.Add(_(EXIDEV_BBA_STR));
		SP1Devices.Add(_(EXIDEV_AM_BB_STR));


	for (int i = 0; i < 3; ++i)
	{
		bool isMemcard = false;

		// Add strings to the wxChoice list, the third wxChoice is the SP1 slot
		if (i == 2)
			GCEXIDevice[i]->Append(SP1Devices);
		else
			GCEXIDevice[i]->Append(SlotDevices);

		switch (SConfig::GetInstance().m_EXIDevice[i])
		{
		case EXIDEVICE_NONE:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[0]);
			break;
		case EXIDEVICE_MEMORYCARD:
			isMemcard = GCEXIDevice[i]->SetStringSelection(SlotDevices[2]);
			break;
		case EXIDEVICE_MEMORYCARDFOLDER:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[3]);
			break;
		case EXIDEVICE_GECKO:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[4]);
			break;
		case EXIDEVICE_AGP:
			isMemcard = GCEXIDevice[i]->SetStringSelection(SlotDevices[5]);
			break;
		case EXIDEVICE_MIC:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[6]);
			break;
		case EXIDEVICE_ETH:
			GCEXIDevice[i]->SetStringSelection(SP1Devices[2]);
			break;
		case EXIDEVICE_AM_BASEBOARD:
			GCEXIDevice[i]->SetStringSelection(SP1Devices[3]);
			break;
		case EXIDEVICE_DUMMY:
		default:
			GCEXIDevice[i]->SetStringSelection(SlotDevices[1]);
			break;
		}
		if (!isMemcard && i < 2)
			GCMemcardPath[i]->Disable();
	}

	// Wii - Misc
	WiiScreenSaver->SetValue(!!SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.SSV"));
	WiiPAL60->SetValue(!!SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.E60"));
	WiiAspectRatio->SetSelection(SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.AR"));
	WiiSystemLang->SetSelection(SConfig::GetInstance().m_SYSCONF->GetData<u8>("IPL.LNG"));

	// Wii - Devices
	WiiSDCard->SetValue(SConfig::GetInstance().m_WiiSDCard);
	WiiKeyboard->SetValue(SConfig::GetInstance().m_WiiKeyboard);


	// Paths
	RecursiveISOPath->SetValue(SConfig::GetInstance().m_RecursiveISOFolder);
	DefaultISO->SetPath(StrToWxStr(startup_params.m_strDefaultISO));
	DVDRoot->SetPath(StrToWxStr(startup_params.m_strDVDRoot));
	ApploaderPath->SetPath(StrToWxStr(startup_params.m_strApploader));
	NANDRoot->SetPath(StrToWxStr(SConfig::GetInstance().m_NANDPath));
}

void CConfigMain::InitializeGUITooltips()
{
	// General - Basic
	CPUThread->SetToolTip(_("Splits the CPU and GPU threads so they can be run on separate cores.\nProvides major speed improvements on most modern PCs, but can cause occasional crashes/glitches."));
	SkipIdle->SetToolTip(_("Attempt to detect and skip wait-loops.\nIf unsure, leave this checked."));
	EnableCheats->SetToolTip(_("Enables the use of Action Replay and Gecko cheats."));
	Framelimit->SetToolTip(_("Limits the game speed to the specified number of frames per second (full speed is 60 for NTSC and 50 for PAL)."));

	// General - Advanced
	_NTSCJ->SetToolTip(_("Forces NTSC-J mode for using the Japanese ROM font.\nIf left unchecked, Dolphin defaults to NTSC-U and automatically enables this setting when playing Japanese games."));

	// Display - Interface
	ConfirmStop->SetToolTip(_("Show a confirmation box before stopping a game."));
	UsePanicHandlers->SetToolTip(_("Show a message box when a potentially serious error has occurred.\nDisabling this may avoid annoying and non-fatal messages, but it may result in major crashes having no explanation at all."));
	OnScreenDisplayMessages->SetToolTip(_("Display messages over the emulation screen area.\nThese messages include memory card writes, video backend and CPU information, and JIT cache clearing."));

	InterfaceLang->SetToolTip(_("Change the language of the user interface.\nRequires restart."));

	// Audio - Backend
	BackendSelection->SetToolTip(_("Changing this will have no effect while the emulator is running."));

	// GameCube - IPL
	GCSystemLang->SetToolTip(_("Sets the GameCube system language."));

	// GameCube - Devices
	GCEXIDevice[2]->SetToolTip(_("Serial Port 1 - This is the port which devices such as the net adapter use."));

	// Wii - Misc
	WiiScreenSaver->SetToolTip(_("Dims the screen after five minutes of inactivity."));
	WiiPAL60->SetToolTip(_("Sets the Wii display mode to 60Hz (480i) instead of 50Hz (576i) for PAL games.\nMay not work for all games."));
	WiiSystemLang->SetToolTip(_("Sets the Wii system language."));

	// Wii - Devices
	WiiSDCard->SetToolTip(_("Saved to /Wii/sd.raw (default size is 128mb)"));
	WiiKeyboard->SetToolTip(_("May cause slow down in Wii Menu and some games."));

#if defined(__APPLE__)
	DPL2Decoder->SetToolTip(_("Enables Dolby Pro Logic II emulation using 5.1 surround. Not available on OS X."));
#else
	DPL2Decoder->SetToolTip(_("Enables Dolby Pro Logic II emulation using 5.1 surround. OpenAL or Pulse backends only."));
#endif

	Latency->SetToolTip(_("Sets the latency (in ms). Higher values may reduce audio crackling. OpenAL backend only."));
}

void CConfigMain::CreateGUIControls()
{
	InitializeGUILists();

	// Create the notebook and pages
	Notebook = new wxNotebook(this, ID_NOTEBOOK);
	wxPanel* const GeneralPage = new wxPanel(Notebook, ID_GENERALPAGE);
	wxPanel* const DisplayPage = new wxPanel(Notebook, ID_DISPLAYPAGE);
	wxPanel* const AudioPage = new wxPanel(Notebook, ID_AUDIOPAGE);
	wxPanel* const GamecubePage = new wxPanel(Notebook, ID_GAMECUBEPAGE);
	wxPanel* const WiiPage = new wxPanel(Notebook, ID_WIIPAGE);
	wxPanel* const AdvancedPage = new wxPanel(Notebook, ID_ADVANCEDPAGE);
	PathsPage = new wxPanel(Notebook, ID_PATHSPAGE);

	Notebook->AddPage(GeneralPage, _("General"));
	Notebook->AddPage(DisplayPage, _("Interface"));
	Notebook->AddPage(AudioPage, _("Audio"));
	Notebook->AddPage(GamecubePage, _("GameCube"));
	Notebook->AddPage(WiiPage, _("Wii"));
	Notebook->AddPage(PathsPage, _("Paths"));
	Notebook->AddPage(AdvancedPage, _("Advanced"));

	// General page
	// Core Settings - Basic
	CPUThread = new wxCheckBox(GeneralPage, ID_CPUTHREAD, _("Enable Dual Core (speedup)"));
	SkipIdle = new wxCheckBox(GeneralPage, ID_IDLESKIP, _("Enable Idle Skipping (speedup)"));
	EnableCheats = new wxCheckBox(GeneralPage, ID_ENABLECHEATS, _("Enable Cheats"));
	// Framelimit
	Framelimit = new wxChoice(GeneralPage, ID_FRAMELIMIT, wxDefaultPosition, wxDefaultSize, arrayStringFor_Framelimit);
	// Core Settings - Advanced
	CPUEngine = new wxRadioBox(GeneralPage, ID_CPUENGINE, _("CPU Emulator Engine"), wxDefaultPosition, wxDefaultSize, arrayStringFor_CPUEngine, 0, wxRA_SPECIFY_ROWS);
	_NTSCJ = new wxCheckBox(GeneralPage, ID_NTSCJ, _("Force Console as NTSC-J"));

	// Populate the General settings
	wxBoxSizer* sFramelimit = new wxBoxSizer(wxHORIZONTAL);
	sFramelimit->Add(TEXT_BOX(GeneralPage, _("Framelimit:")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	sFramelimit->Add(Framelimit, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	wxStaticBoxSizer* const sbBasic = new wxStaticBoxSizer(wxVERTICAL, GeneralPage, _("Basic Settings"));
	sbBasic->Add(CPUThread, 0, wxALL, 5);
	sbBasic->Add(SkipIdle, 0, wxALL, 5);
	sbBasic->Add(EnableCheats, 0, wxALL, 5);
	sbBasic->Add(sFramelimit);

	wxStaticBoxSizer* const sbAdvanced = new wxStaticBoxSizer(wxVERTICAL, GeneralPage, _("Advanced Settings"));
	sbAdvanced->Add(CPUEngine, 0, wxALL, 5);
	sbAdvanced->Add(_NTSCJ, 0, wxALL, 5);

	wxBoxSizer* const sGeneralPage = new wxBoxSizer(wxVERTICAL);
	sGeneralPage->Add(sbBasic, 0, wxEXPAND | wxALL, 5);
	sGeneralPage->Add(sbAdvanced, 0, wxEXPAND | wxALL, 5);
	GeneralPage->SetSizer(sGeneralPage);

	// Interface Language
	InterfaceLang = new wxChoice(DisplayPage, ID_INTERFACE_LANG, wxDefaultPosition, wxDefaultSize, arrayStringFor_InterfaceLang);
	// Hotkey configuration
	HotkeyConfig = new wxButton(DisplayPage, ID_HOTKEY_CONFIG, _("Hotkeys"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	// Interface settings
	ConfirmStop = new wxCheckBox(DisplayPage, ID_INTERFACE_CONFIRMSTOP, _("Confirm on Stop"));
	UsePanicHandlers = new wxCheckBox(DisplayPage, ID_INTERFACE_USEPANICHANDLERS, _("Use Panic Handlers"));
	OnScreenDisplayMessages = new wxCheckBox(DisplayPage, ID_INTERFACE_ONSCREENDISPLAYMESSAGES, _("On-Screen Display Messages"));

	wxBoxSizer* sInterface = new wxBoxSizer(wxHORIZONTAL);
	sInterface->Add(TEXT_BOX(DisplayPage, _("Language:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sInterface->Add(InterfaceLang, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sInterface->AddStretchSpacer();
	sInterface->Add(HotkeyConfig, 0, wxALIGN_RIGHT | wxALL, 5);

	// theme selection
	auto const theme_selection = new wxChoice(DisplayPage, wxID_ANY);

	CFileSearch::XStringVector theme_dirs;
	theme_dirs.push_back(File::GetUserPath(D_THEMES_IDX));
	theme_dirs.push_back(File::GetSysDirectory() + THEMES_DIR);

	CFileSearch cfs(CFileSearch::XStringVector(1, "*"), theme_dirs);
	auto const& sv = cfs.GetFileNames();
	std::for_each(sv.begin(), sv.end(), [theme_selection](const std::string& filename)
	{
		std::string name, ext;
		SplitPath(filename, nullptr, &name, &ext);

		name += ext;
		auto const wxname = StrToWxStr(name);
		if (-1 == theme_selection->FindString(wxname))
			theme_selection->Append(wxname);
	});

	theme_selection->SetStringSelection(StrToWxStr(SConfig::GetInstance().m_LocalCoreStartupParameter.theme_name));

	// std::function = avoid error on msvc
	theme_selection->Bind(wxEVT_CHOICE, std::function<void(wxEvent&)>([theme_selection](wxEvent&)
	{
		SConfig::GetInstance().m_LocalCoreStartupParameter.theme_name = WxStrToStr(theme_selection->GetStringSelection());
		main_frame->InitBitmaps();
		main_frame->UpdateGameList();
	}));

	auto const scInterface = new wxBoxSizer(wxHORIZONTAL);
	scInterface->Add(TEXT_BOX(DisplayPage, _("Theme:")), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	scInterface->Add(theme_selection, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	scInterface->AddStretchSpacer();

	sbInterface = new wxStaticBoxSizer(wxVERTICAL, DisplayPage, _("Interface Settings"));
	sbInterface->Add(ConfirmStop, 0, wxALL, 5);
	sbInterface->Add(UsePanicHandlers, 0, wxALL, 5);
	sbInterface->Add(OnScreenDisplayMessages, 0, wxALL, 5);
	sbInterface->Add(scInterface, 0, wxEXPAND | wxALL, 5);
	sbInterface->Add(sInterface, 0, wxEXPAND | wxALL, 5);
	sDisplayPage = new wxBoxSizer(wxVERTICAL);
	sDisplayPage->Add(sbInterface, 0, wxEXPAND | wxALL, 5);
	DisplayPage->SetSizer(sDisplayPage);


	// Audio page
	DSPEngine = new wxRadioBox(AudioPage, ID_DSPENGINE, _("DSP Emulator Engine"), wxDefaultPosition, wxDefaultSize, arrayStringFor_DSPEngine, 0, wxRA_SPECIFY_ROWS);
	DPL2Decoder = new wxCheckBox(AudioPage, ID_DPL2DECODER, _("Dolby Pro Logic II decoder"));
	VolumeSlider = new wxSlider(AudioPage, ID_VOLUME, 0, 0, 100, wxDefaultPosition, wxDefaultSize, wxSL_VERTICAL | wxSL_INVERSE);
	VolumeText = new wxStaticText(AudioPage, wxID_ANY, "");
	BackendSelection = new wxChoice(AudioPage, ID_BACKEND, wxDefaultPosition, wxDefaultSize, wxArrayBackends, 0, wxDefaultValidator, wxEmptyString);
	Latency = new wxSpinCtrl(AudioPage, ID_LATENCY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 30);

	Latency->Bind(wxEVT_SPINCTRL, &CConfigMain::AudioSettingsChanged, this);

	if (Core::IsRunning())
	{
		Latency->Disable();
		BackendSelection->Disable();
		DPL2Decoder->Disable();
	}

	// Create sizer and add items to dialog
	wxStaticBoxSizer *sbAudioSettings = new wxStaticBoxSizer(wxVERTICAL, AudioPage, _("Sound Settings"));
	sbAudioSettings->Add(DSPEngine, 0, wxALL | wxEXPAND, 5);
	sbAudioSettings->Add(DPL2Decoder, 0, wxALL, 5);

	wxStaticBoxSizer *sbVolume = new wxStaticBoxSizer(wxVERTICAL, AudioPage, _("Volume"));
	sbVolume->Add(VolumeSlider, 1, wxLEFT | wxRIGHT, 13);
	sbVolume->Add(VolumeText, 0, wxALIGN_CENTER | wxALL, 5);

	wxGridBagSizer *sBackend = new wxGridBagSizer();
	sBackend->Add(TEXT_BOX(AudioPage, _("Audio Backend:")), wxGBPosition(0, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sBackend->Add(BackendSelection, wxGBPosition(0, 1), wxDefaultSpan, wxALL, 5);
	sBackend->Add(TEXT_BOX(AudioPage, _("Latency:")), wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sBackend->Add(Latency, wxGBPosition(1, 1), wxDefaultSpan, wxALL, 5);
	wxStaticBoxSizer *sbBackend = new wxStaticBoxSizer(wxHORIZONTAL, AudioPage, _("Backend Settings"));
	sbBackend->Add(sBackend, 0, wxEXPAND);

	wxBoxSizer *sAudio = new wxBoxSizer(wxHORIZONTAL);
	sAudio->Add(sbAudioSettings, 1, wxEXPAND | wxALL, 5);
	sAudio->Add(sbVolume, 0, wxEXPAND | wxALL, 5);

	sAudioPage = new wxBoxSizer(wxVERTICAL);
	sAudioPage->Add(sAudio, 0, wxALL | wxEXPAND);
	sAudioPage->Add(sbBackend, 0, wxALL | wxEXPAND, 5);
	AudioPage->SetSizerAndFit(sAudioPage);


	// GameCube page
	// IPL settings
	GCSystemLang = new wxChoice(GamecubePage, ID_GC_SRAM_LNG, wxDefaultPosition, wxDefaultSize, arrayStringFor_GCSystemLang);
	GCAlwaysHLE_BS2 = new wxCheckBox(GamecubePage, ID_GC_ALWAYS_HLE_BS2, _("Skip BIOS"));

	if (!File::Exists(File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + USA_DIR + DIR_SEP GC_IPL) &&
	    !File::Exists(File::GetSysDirectory() + GC_SYS_DIR + DIR_SEP + USA_DIR + DIR_SEP GC_IPL) &&
	    !File::Exists(File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + JAP_DIR + DIR_SEP GC_IPL) &&
	    !File::Exists(File::GetSysDirectory() + GC_SYS_DIR + DIR_SEP + JAP_DIR + DIR_SEP GC_IPL) &&
	    !File::Exists(File::GetUserPath(D_GCUSER_IDX) + DIR_SEP + EUR_DIR + DIR_SEP GC_IPL) &&
	    !File::Exists(File::GetSysDirectory() + GC_SYS_DIR + DIR_SEP + EUR_DIR + DIR_SEP GC_IPL))
	{
		GCAlwaysHLE_BS2->Disable();
		GCAlwaysHLE_BS2->SetToolTip(_("Put BIOS roms in User/GC/{region}."));
	}

	// Device settings
	// EXI Devices
	wxStaticText* GCEXIDeviceText[3];
	GCEXIDeviceText[0] = TEXT_BOX(GamecubePage, _("Slot A"));
	GCEXIDeviceText[1] = TEXT_BOX(GamecubePage, _("Slot B"));
	GCEXIDeviceText[2] = TEXT_BOX(GamecubePage, "SP1");
	GCEXIDevice[0] = new wxChoice(GamecubePage, ID_GC_EXIDEVICE_SLOTA);
	GCEXIDevice[1] = new wxChoice(GamecubePage, ID_GC_EXIDEVICE_SLOTB);
	GCEXIDevice[2] = new wxChoice(GamecubePage, ID_GC_EXIDEVICE_SP1);
	GCMemcardPath[0] = new wxButton(GamecubePage, ID_GC_EXIDEVICE_SLOTA_PATH, "...",
			wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	GCMemcardPath[1] = new wxButton(GamecubePage, ID_GC_EXIDEVICE_SLOTB_PATH, "...",
			wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);

	// Populate the GameCube page
	sGamecubeIPLSettings = new wxGridBagSizer();
	sGamecubeIPLSettings->Add(GCAlwaysHLE_BS2, wxGBPosition(0, 0), wxGBSpan(1, 2), wxALL, 5);
	sGamecubeIPLSettings->Add(TEXT_BOX(GamecubePage, _("System Language:")),
			wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	sGamecubeIPLSettings->Add(GCSystemLang, wxGBPosition(1, 1), wxDefaultSpan, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	sbGamecubeIPLSettings = new wxStaticBoxSizer(wxVERTICAL, GamecubePage, _("IPL Settings"));
	sbGamecubeIPLSettings->Add(sGamecubeIPLSettings);
	wxStaticBoxSizer *sbGamecubeDeviceSettings = new wxStaticBoxSizer(wxVERTICAL, GamecubePage, _("Device Settings"));
	wxGridBagSizer* sbGamecubeEXIDevSettings = new wxGridBagSizer(10, 10);
	for (int i = 0; i < 3; ++i)
	{
		sbGamecubeEXIDevSettings->Add(GCEXIDeviceText[i], wxGBPosition(i, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		sbGamecubeEXIDevSettings->Add(GCEXIDevice[i], wxGBPosition(i, 1), wxGBSpan(1, (i < 2) ? 1 : 2), wxALIGN_CENTER_VERTICAL);
		if (i < 2)
			sbGamecubeEXIDevSettings->Add(GCMemcardPath[i], wxGBPosition(i, 2), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		if (NetPlay::IsNetPlayRunning())
			GCEXIDevice[i]->Disable();
	}
	sbGamecubeDeviceSettings->Add(sbGamecubeEXIDevSettings, 0, wxALL, 5);

	sGamecubePage = new wxBoxSizer(wxVERTICAL);
	sGamecubePage->Add(sbGamecubeIPLSettings, 0, wxEXPAND | wxALL, 5);
	sGamecubePage->Add(sbGamecubeDeviceSettings, 0, wxEXPAND | wxALL, 5);
	GamecubePage->SetSizer(sGamecubePage);

	// Wii page
	// Misc Settings
	WiiScreenSaver = new wxCheckBox(WiiPage, ID_WII_IPL_SSV, _("Enable Screen Saver"));
	WiiPAL60 = new wxCheckBox(WiiPage, ID_WII_IPL_E60, _("Use PAL60 Mode (EuRGB60)"));
	WiiAspectRatio = new wxChoice(WiiPage, ID_WII_IPL_AR, wxDefaultPosition, wxDefaultSize, arrayStringFor_WiiAspectRatio);
	WiiSystemLang = new wxChoice(WiiPage, ID_WII_IPL_LNG, wxDefaultPosition, wxDefaultSize, arrayStringFor_WiiSystemLang);

	// Device Settings
	WiiSDCard = new wxCheckBox(WiiPage, ID_WII_SD_CARD, _("Insert SD Card"));
	WiiKeyboard = new wxCheckBox(WiiPage, ID_WII_KEYBOARD, _("Connect USB Keyboard"));

	// Populate the Wii Page
	sWiiIPLSettings = new wxGridBagSizer();
	sWiiIPLSettings->Add(WiiScreenSaver, wxGBPosition(0, 0), wxGBSpan(1, 2), wxALL, 5);
	sWiiIPLSettings->Add(WiiPAL60, wxGBPosition(1, 0), wxGBSpan(1, 2), wxALL, 5);
	sWiiIPLSettings->Add(TEXT_BOX(WiiPage, _("Aspect Ratio:")),
			wxGBPosition(2, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sWiiIPLSettings->Add(WiiAspectRatio, wxGBPosition(2, 1), wxDefaultSpan, wxALL, 5);
	sWiiIPLSettings->Add(TEXT_BOX(WiiPage, _("System Language:")),
			wxGBPosition(3, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sWiiIPLSettings->Add(WiiSystemLang, wxGBPosition(3, 1), wxDefaultSpan, wxALL, 5);
	sbWiiIPLSettings = new wxStaticBoxSizer(wxVERTICAL, WiiPage, _("Misc Settings"));
	sbWiiIPLSettings->Add(sWiiIPLSettings);

	sbWiiDeviceSettings = new wxStaticBoxSizer(wxVERTICAL, WiiPage, _("Device Settings"));
	sbWiiDeviceSettings->Add(WiiSDCard, 0, wxALL, 5);
	sbWiiDeviceSettings->Add(WiiKeyboard, 0, wxALL, 5);

	sWiiPage = new wxBoxSizer(wxVERTICAL);
	sWiiPage->Add(sbWiiIPLSettings, 0, wxEXPAND | wxALL, 5);
	sWiiPage->Add(sbWiiDeviceSettings, 0, wxEXPAND | wxALL, 5);
	WiiPage->SetSizer(sWiiPage);


	// Paths page
	ISOPaths = new wxListBox(PathsPage, ID_ISOPATHS, wxDefaultPosition, wxDefaultSize, arrayStringFor_ISOPaths, wxLB_SINGLE);
	RecursiveISOPath = new wxCheckBox(PathsPage, ID_RECURSIVEISOPATH, _("Search Subfolders"));
	AddISOPath = new wxButton(PathsPage, ID_ADDISOPATH, _("Add..."));
	RemoveISOPath = new wxButton(PathsPage, ID_REMOVEISOPATH, _("Remove"));
	RemoveISOPath->Disable();

	DefaultISO = new wxFilePickerCtrl(PathsPage, ID_DEFAULTISO, wxEmptyString, _("Choose a default ISO:"),
		_("All GC/Wii files (elf, dol, gcm, iso, wbfs, ciso, gcz, wad)") + wxString::Format("|*.elf;*.dol;*.gcm;*.iso;*.wbfs;*.ciso;*.gcz;*.wad|%s", wxGetTranslation(wxALL_FILES)),
		wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL | wxFLP_OPEN);
	DVDRoot = new wxDirPickerCtrl(PathsPage, ID_DVDROOT, wxEmptyString, _("Choose a DVD root directory:"), wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL);
	ApploaderPath = new wxFilePickerCtrl(PathsPage, ID_APPLOADERPATH, wxEmptyString, _("Choose file to use as apploader: (applies to discs constructed from directories only)"),
		_("apploader (.img)") + wxString::Format("|*.img|%s", wxGetTranslation(wxALL_FILES)),
		wxDefaultPosition, wxDefaultSize, wxFLP_USE_TEXTCTRL | wxFLP_OPEN);
	NANDRoot = new wxDirPickerCtrl(PathsPage, ID_NANDROOT, wxEmptyString, _("Choose a NAND root directory:"), wxDefaultPosition, wxDefaultSize, wxDIRP_USE_TEXTCTRL);

	// Populate the settings
	wxBoxSizer* sISOButtons = new wxBoxSizer(wxHORIZONTAL);
	sISOButtons->Add(RecursiveISOPath, 0, wxALL | wxALIGN_CENTER, 0);
	sISOButtons->AddStretchSpacer();
	sISOButtons->Add(AddISOPath, 0, wxALL, 0);
	sISOButtons->Add(RemoveISOPath, 0, wxALL, 0);
	sbISOPaths = new wxStaticBoxSizer(wxVERTICAL, PathsPage, _("ISO Directories"));
	sbISOPaths->Add(ISOPaths, 1, wxEXPAND | wxALL, 0);
	sbISOPaths->Add(sISOButtons, 0, wxEXPAND | wxALL, 5);

	sOtherPaths = new wxGridBagSizer();
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("Default ISO:")),
			wxGBPosition(0, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sOtherPaths->Add(DefaultISO, wxGBPosition(0, 1), wxDefaultSpan, wxEXPAND | wxALL, 5);
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("DVD Root:")),
			wxGBPosition(1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sOtherPaths->Add(DVDRoot, wxGBPosition(1, 1), wxDefaultSpan, wxEXPAND | wxALL, 5);
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("Apploader:")),
			wxGBPosition(2, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sOtherPaths->Add(ApploaderPath, wxGBPosition(2, 1), wxDefaultSpan, wxEXPAND | wxALL, 5);
	sOtherPaths->Add(TEXT_BOX(PathsPage, _("Wii NAND Root:")),
			wxGBPosition(3, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL | wxALL, 5);
	sOtherPaths->Add(NANDRoot, wxGBPosition(3, 1), wxDefaultSpan, wxEXPAND | wxALL, 5);
	sOtherPaths->AddGrowableCol(1);

	// Populate the Paths page
	sPathsPage = new wxBoxSizer(wxVERTICAL);
	sPathsPage->Add(sbISOPaths, 1, wxEXPAND | wxALL, 5);
	sPathsPage->Add(sOtherPaths, 0, wxEXPAND | wxALL, 5);
	PathsPage->SetSizer(sPathsPage);

	wxBoxSizer* sMain = new wxBoxSizer(wxVERTICAL);
	sMain->Add(Notebook, 1, wxEXPAND | wxALL, 5);
	sMain->Add(CreateButtonSizer(wxOK), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	wxStaticBoxSizer* sbCPUOptions = new wxStaticBoxSizer(wxVERTICAL, AdvancedPage, _("CPU Options"));
	wxBoxSizer* bOverclockEnable = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bOverclock = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bOverclockDesc = new wxBoxSizer(wxHORIZONTAL);
	EnableOC = new wxCheckBox(AdvancedPage, ID_ENABLEOVERCLOCK, _("Enable CPU Clock Override"));
	OCSlider = new wxSlider(AdvancedPage, ID_OVERCLOCK, 100, 0, 150, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	wxStaticText* OCDescription = new wxStaticText(AdvancedPage, wxID_ANY,
	  _("Higher values can make variable-framerate games\n"
	    "run at a higher framerate, at the expense of CPU.\n"
	    "Lower values can make variable-framerate games\n"
	    "run at a lower framerate, saving CPU.\n\n"
	    "WARNING: Changing this from the default (100%)\n"
	    "can and will break games and cause glitches.\n"
	    "Do so at your own risk. Please do not report\n"
	    "bugs that occur with a non-default clock.\n"));
	OCText = new wxStaticText(AdvancedPage, wxID_ANY, "");
	bOverclockEnable->Add(EnableOC);
	bOverclock->Add(OCSlider, 1, wxALL, 5);
	bOverclock->Add(OCText, 1, wxALL, 5);
	bOverclockDesc->Add(OCDescription, 1, wxALL, 5);
	sbCPUOptions->Add(bOverclockEnable);
	sbCPUOptions->Add(bOverclock);
	sbCPUOptions->Add(bOverclockDesc);
	wxBoxSizer* const sAdvancedPage = new wxBoxSizer(wxVERTICAL);
	sAdvancedPage->Add(sbCPUOptions, 0, wxEXPAND | wxALL, 5);
	AdvancedPage->SetSizer(sAdvancedPage);

	InitializeGUIValues();
	InitializeGUITooltips();

	UpdateGUI();

	SetSizerAndFit(sMain);
	Center();
	SetFocus();
}

void CConfigMain::OnClose(wxCloseEvent& WXUNUSED(event))
{
	EndModal((bRefreshList) ? wxID_OK : wxID_CANCEL);
}

void CConfigMain::OnOk(wxCommandEvent& WXUNUSED(event))
{
	Close();

	// Save the config. Dolphin crashes too often to only save the settings on closing
	SConfig::GetInstance().SaveSettings();
}

void CConfigMain::UpdateCPUClock()
{
	bool wii = SConfig::GetInstance().m_LocalCoreStartupParameter.bWii;
	int percent = (int)(roundf(SConfig::GetInstance().m_OCFactor * 100.f));
	int clock = (int)(roundf(SConfig::GetInstance().m_OCFactor * (wii ? 729.f : 486.f)));
	OCText->SetLabel(SConfig::GetInstance().m_OCEnable ? wxString::Format("%d %% (%d mhz)", percent, clock) : "");
}

// Core settings
void CConfigMain::CoreSettingsChanged(wxCommandEvent& event)
{
	SCoreStartupParameter& startup_params = SConfig::GetInstance().m_LocalCoreStartupParameter;

	switch (event.GetId())
	{
	// Core - Basic
	case ID_CPUTHREAD:
		if (Core::IsRunning())
			return;
		startup_params.bCPUThread = CPUThread->IsChecked();
		break;
	case ID_IDLESKIP:
		startup_params.bSkipIdle = SkipIdle->IsChecked();
		break;
	case ID_ENABLECHEATS:
		startup_params.bEnableCheats = EnableCheats->IsChecked();
		break;
	case ID_FRAMELIMIT:
		SConfig::GetInstance().m_Framelimit = Framelimit->GetSelection();
		break;
	// Core - Advanced
	case ID_CPUENGINE:
		startup_params.iCPUCore = CPUCores[CPUEngine->GetSelection()].CPUid;
		if (main_frame->g_pCodeWindow)
		{
			bool using_interp = (startup_params.iCPUCore == PowerPC::CORE_INTERPRETER);
			main_frame->g_pCodeWindow->GetMenuBar()->Check(IDM_INTERPRETER, using_interp);
		}
		break;
	case ID_NTSCJ:
		startup_params.bForceNTSCJ = _NTSCJ->IsChecked();
		break;
	case ID_ENABLEOVERCLOCK:
		SConfig::GetInstance().m_OCEnable = EnableOC->IsChecked();
		OCSlider->Enable(SConfig::GetInstance().m_OCEnable);
		UpdateCPUClock();
		break;
	case ID_OVERCLOCK:
		// Vaguely exponential scaling?
		SConfig::GetInstance().m_OCFactor = exp2f((OCSlider->GetValue() - 100.f) / 25.f);
		UpdateCPUClock();
		break;
	}
}

// Display and Interface settings
void CConfigMain::DisplaySettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	// Display - Interface
	case ID_INTERFACE_CONFIRMSTOP:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bConfirmStop = ConfirmStop->IsChecked();
		break;
	case ID_INTERFACE_USEPANICHANDLERS:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bUsePanicHandlers = UsePanicHandlers->IsChecked();
		SetEnableAlert(UsePanicHandlers->IsChecked());
		break;
	case ID_INTERFACE_ONSCREENDISPLAYMESSAGES:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bOnScreenDisplayMessages = OnScreenDisplayMessages->IsChecked();
		SetEnableAlert(OnScreenDisplayMessages->IsChecked());
		break;
	case ID_INTERFACE_LANG:
		if (SConfig::GetInstance().m_InterfaceLanguage != langIds[InterfaceLang->GetSelection()])
			SuccessAlertT("You must restart Dolphin in order for the change to take effect.");
		SConfig::GetInstance().m_InterfaceLanguage = langIds[InterfaceLang->GetSelection()];
		break;
	case ID_HOTKEY_CONFIG:
		{
			bool was_init = false;

			InputConfig* const hotkey_plugin = HotkeyManagerEmu::GetConfig();

			// check if game is running
			if (g_controller_interface.IsInit())
			{
				was_init = true;
			}
			else
			{
#if defined(HAVE_X11) && HAVE_X11
				Window win = X11Utils::XWindowFromHandle(GetHandle());
				HotkeyManagerEmu::Initialize(reinterpret_cast<void*>(win));
#else
				HotkeyManagerEmu::Initialize(reinterpret_cast<void*>(GetHandle()));
#endif
			}

			InputConfigDialog m_ConfigFrame(this, *hotkey_plugin, _("Dolphin Hotkeys"));
			m_ConfigFrame.ShowModal();

			// if game isn't running
			if (!was_init)
			{
				HotkeyManagerEmu::Shutdown();
			}
		}
		// Update the GUI in case menu accelerators were changed
		main_frame->UpdateGUI();
		break;
	}
}

void CConfigMain::AudioSettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	case ID_DSPENGINE:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bDSPHLE = DSPEngine->GetSelection() == 0;
		SConfig::GetInstance().m_DSPEnableJIT = DSPEngine->GetSelection() == 1;
		AudioCommon::UpdateSoundStream();
		break;

	case ID_VOLUME:
		SConfig::GetInstance().m_Volume = VolumeSlider->GetValue();
		AudioCommon::UpdateSoundStream();
		VolumeText->SetLabel(wxString::Format("%d %%", VolumeSlider->GetValue()));
		break;

	case ID_DPL2DECODER:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bDPL2Decoder = DPL2Decoder->IsChecked();
		break;

	case ID_BACKEND:
		VolumeSlider->Enable(SupportsVolumeChanges(WxStrToStr(BackendSelection->GetStringSelection())));
		Latency->Enable(WxStrToStr(BackendSelection->GetStringSelection()) == BACKEND_OPENAL);
		DPL2Decoder->Enable(WxStrToStr(BackendSelection->GetStringSelection()) == BACKEND_OPENAL
				|| WxStrToStr(BackendSelection->GetStringSelection()) == BACKEND_PULSEAUDIO);
		// Don't save the translated BACKEND_NULLSOUND string
		SConfig::GetInstance().sBackend = BackendSelection->GetSelection() ?
			WxStrToStr(BackendSelection->GetStringSelection()) : BACKEND_NULLSOUND;
		AudioCommon::UpdateSoundStream();
		break;

	case ID_LATENCY:
		SConfig::GetInstance().m_LocalCoreStartupParameter.iLatency = Latency->GetValue();
		break;

	default:
		break;
	}
}

void CConfigMain::AddAudioBackends()
{
	for (const std::string& backend : AudioCommon::GetSoundBackends())
	{
		BackendSelection->Append(wxGetTranslation(StrToWxStr(backend)));
		int num = BackendSelection->
			FindString(StrToWxStr(SConfig::GetInstance().sBackend));
		BackendSelection->SetSelection(num);
	}
}

bool CConfigMain::SupportsVolumeChanges(std::string backend)
{
	//FIXME: this one should ask the backend whether it supports it.
	//       but getting the backend from string etc. is probably
	//       too much just to enable/disable a stupid slider...
	return (backend == BACKEND_COREAUDIO ||
			backend == BACKEND_OPENAL ||
			backend == BACKEND_XAUDIO2);
}


// GC settings
// -----------------------
void CConfigMain::GCSettingsChanged(wxCommandEvent& event)
{
	int exidevice = 0;
	switch (event.GetId())
	{
	// GameCube - IPL
	case ID_GC_SRAM_LNG:
		SConfig::GetInstance().m_LocalCoreStartupParameter.SelectedLanguage = GCSystemLang->GetSelection();
		bRefreshList = true;
		break;
	// GameCube - IPL Settings
	case ID_GC_ALWAYS_HLE_BS2:
		SConfig::GetInstance().m_LocalCoreStartupParameter.bHLE_BS2 = GCAlwaysHLE_BS2->IsChecked();
		break;
	// GameCube - Devices
	case ID_GC_EXIDEVICE_SP1:
		exidevice++;
	case ID_GC_EXIDEVICE_SLOTB:
		exidevice++;
	case ID_GC_EXIDEVICE_SLOTA:
		ChooseEXIDevice(event.GetString(), exidevice);
		break;
	case ID_GC_EXIDEVICE_SLOTA_PATH:
		ChooseSlotPath(true, SConfig::GetInstance().m_EXIDevice[0]);
		break;
	case ID_GC_EXIDEVICE_SLOTB_PATH:
		ChooseSlotPath(false, SConfig::GetInstance().m_EXIDevice[1]);
		break;
	}
}

void CConfigMain::ChooseSlotPath(bool isSlotA, TEXIDevices device_type)
{
	bool memcard = (device_type == EXIDEVICE_MEMORYCARD);
	std::string path;
	std::string cardname;
	std::string ext;
	std::string pathA = SConfig::GetInstance().m_strMemoryCardA;
	std::string pathB = SConfig::GetInstance().m_strMemoryCardB;
	if (!memcard)
	{
		pathA = SConfig::GetInstance().m_strGbaCartA;
		pathB = SConfig::GetInstance().m_strGbaCartB;
	}
	SplitPath(isSlotA ? pathA : pathB, &path, &cardname, &ext);
	std::string filename = WxStrToStr(wxFileSelector(
		_("Choose a file to open"),
		StrToWxStr(path),
		StrToWxStr(cardname),
		StrToWxStr(ext),
		memcard ? _("GameCube Memory Cards (*.raw,*.gcp)") + "|*.raw;*.gcp" : _("Game Boy Advance Carts (*.gba)") + "|*.gba"));

	if (!filename.empty())
	{
		if (File::Exists(filename))
		{
			if (memcard)
			{
				GCMemcard memorycard(filename);
				if (!memorycard.IsValid())
				{
					WxUtils::ShowErrorDialog(wxString::Format(_("Cannot use that file as a memory card.\n%s\n" \
						"is not a valid GameCube memory card file"), filename.c_str()));
					return;
				}
			}
		}
#ifdef _WIN32
		if (!strncmp(File::GetExeDirectory().c_str(), filename.c_str(), File::GetExeDirectory().size()))
		{
			// If the Exe Directory Matches the prefix of the filename, we still need to verify
			// that the next character is a directory separator character, otherwise we may create an invalid path
			char next_char = filename.at(File::GetExeDirectory().size()) + 1;
			if (next_char == '/' || next_char == '\\')
			{
				filename.erase(0, File::GetExeDirectory().size() + 1);
				filename = "./" + filename;
			}
		}
		std::replace(filename.begin(), filename.end(), '\\', '/');
#endif

		// also check that the path isn't used for the other memcard...
		if (filename.compare(isSlotA ? pathB : pathA) != 0)
		{
			if (memcard)
			{
				if (isSlotA)
					SConfig::GetInstance().m_strMemoryCardA = filename;
				else
					SConfig::GetInstance().m_strMemoryCardB = filename;
			}
			else
			{
				if (isSlotA)
					SConfig::GetInstance().m_strGbaCartA = filename;
				else
					SConfig::GetInstance().m_strGbaCartB = filename;
			}

			if (Core::IsRunning())
			{
				// Change memcard to the new file
				ExpansionInterface::ChangeDevice(
					isSlotA ? 0 : 1, // SlotA: channel 0, SlotB channel 1
					device_type,
					0); // SP1 is device 2, slots are device 0
			}
		}
		else
		{
			WxUtils::ShowErrorDialog(_("Are you trying to use the same file in both slots?"));
		}
	}
}

void CConfigMain::ChooseEXIDevice(wxString deviceName, int deviceNum)
{
	TEXIDevices tempType;

	if (!deviceName.compare(_(EXIDEV_MEMCARD_STR)))
		tempType = EXIDEVICE_MEMORYCARD;
	else if (!deviceName.compare(_(EXIDEV_MEMDIR_STR)))
		tempType = EXIDEVICE_MEMORYCARDFOLDER;
	else if (!deviceName.compare(_(EXIDEV_MIC_STR)))
		tempType = EXIDEVICE_MIC;
	else if (!deviceName.compare(EXIDEV_BBA_STR))
		tempType = EXIDEVICE_ETH;
	else if (!deviceName.compare(EXIDEV_AGP_STR))
		tempType = EXIDEVICE_AGP;
	else if (!deviceName.compare(_(EXIDEV_AM_BB_STR)))
		tempType = EXIDEVICE_AM_BASEBOARD;
	else if (!deviceName.compare(EXIDEV_GECKO_STR))
		tempType = EXIDEVICE_GECKO;
	else if (!deviceName.compare(_(DEV_NONE_STR)))
		tempType = EXIDEVICE_NONE;
	else
		tempType = EXIDEVICE_DUMMY;

	// Gray out the memcard path button if we're not on a memcard or AGP
	if (tempType == EXIDEVICE_MEMORYCARD || tempType == EXIDEVICE_AGP)
		GCMemcardPath[deviceNum]->Enable();
	else if (deviceNum == 0 || deviceNum == 1)
		GCMemcardPath[deviceNum]->Disable();

	SConfig::GetInstance().m_EXIDevice[deviceNum] = tempType;

	if (Core::IsRunning())
	{
		// Change plugged device! :D
		ExpansionInterface::ChangeDevice(
			(deviceNum == 1) ? 1 : 0,  // SlotB is on channel 1, slotA and SP1 are on 0
			tempType,                  // The device enum to change to
			(deviceNum == 2) ? 2 : 0); // SP1 is device 2, slots are device 0
	}
}




// Wii settings
// -------------------
void CConfigMain::WiiSettingsChanged(wxCommandEvent& event)
{
	switch (event.GetId())
	{
	// Wii - SYSCONF settings
	case ID_WII_IPL_SSV:
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.SSV", WiiScreenSaver->IsChecked());
		break;
	case ID_WII_IPL_E60:
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.E60", WiiPAL60->IsChecked());
		break;
	case ID_WII_IPL_AR:
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.AR", WiiAspectRatio->GetSelection());
		break;
	case ID_WII_IPL_LNG:
	{
		int wii_system_lang = WiiSystemLang->GetSelection();
		SConfig::GetInstance().m_SYSCONF->SetData("IPL.LNG", wii_system_lang);
		u8 country_code = GetSADRCountryCode(wii_system_lang);
		if (!SConfig::GetInstance().m_SYSCONF->SetArrayData("IPL.SADR", &country_code, 1))
		{
			WxUtils::ShowErrorDialog(_("Failed to update country code in SYSCONF"));
		}
		break;
	}
	// Wii - Devices
	case ID_WII_SD_CARD:
		SConfig::GetInstance().m_WiiSDCard = WiiSDCard->IsChecked();
		WII_IPC_HLE_Interface::SDIO_EventNotify();
		break;
	case ID_WII_KEYBOARD:
		SConfig::GetInstance().m_WiiKeyboard = WiiKeyboard->IsChecked();
		break;
	}
}


// Paths settings
// -------------------
void CConfigMain::ISOPathsSelectionChanged(wxCommandEvent& WXUNUSED(event))
{
	RemoveISOPath->Enable(ISOPaths->GetSelection() != wxNOT_FOUND);
}

void CConfigMain::AddRemoveISOPaths(wxCommandEvent& event)
{
	if (event.GetId() == ID_ADDISOPATH)
	{
		wxDirDialog dialog(this, _("Choose a directory to add"), wxGetHomeDir(),
				wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

		if (dialog.ShowModal() == wxID_OK)
		{
			if (ISOPaths->FindString(dialog.GetPath()) != -1)
			{
				WxUtils::ShowErrorDialog(_("The chosen directory is already in the list."));
			}
			else
			{
				bRefreshList = true;
				ISOPaths->Append(dialog.GetPath());
			}
		}
	}
	else
	{
		bRefreshList = true;
		ISOPaths->Delete(ISOPaths->GetSelection());

		// This seems to not be activated on Windows when it should be. wxw bug?
#ifdef _WIN32
		ISOPathsSelectionChanged(wxCommandEvent());
#endif
	}

	// Save changes right away
	SConfig::GetInstance().m_ISOFolder.clear();

	for (unsigned int i = 0; i < ISOPaths->GetCount(); i++)
		SConfig::GetInstance().m_ISOFolder.push_back(WxStrToStr(ISOPaths->GetStrings()[i]));
}

void CConfigMain::RecursiveDirectoryChanged(wxCommandEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_RecursiveISOFolder = RecursiveISOPath->IsChecked();
	bRefreshList = true;
}

void CConfigMain::DefaultISOChanged(wxFileDirPickerEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strDefaultISO = WxStrToStr(DefaultISO->GetPath());
}

void CConfigMain::DVDRootChanged(wxFileDirPickerEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strDVDRoot = WxStrToStr(DVDRoot->GetPath());
}

void CConfigMain::ApploaderPathChanged(wxFileDirPickerEvent& WXUNUSED(event))
{
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strApploader = WxStrToStr(ApploaderPath->GetPath());
}

void CConfigMain::NANDRootChanged(wxFileDirPickerEvent& WXUNUSED(event))
{
	std::string NANDPath =
		SConfig::GetInstance().m_NANDPath =
			File::GetUserPath(D_WIIROOT_IDX, WxStrToStr(NANDRoot->GetPath()));
	NANDRoot->SetPath(StrToWxStr(NANDPath));
	SConfig::GetInstance().m_SYSCONF->UpdateLocation();
	DiscIO::cUIDsys::AccessInstance().UpdateLocation();
	DiscIO::CSharedContent::AccessInstance().UpdateLocation();
	main_frame->UpdateWiiMenuChoice();
}

// GFX backend selection
void CConfigMain::OnSelectionChanged(wxCommandEvent& ev)
{
	g_video_backend = g_available_video_backends[ev.GetInt()];
	SConfig::GetInstance().m_LocalCoreStartupParameter.m_strVideoBackend = g_video_backend->GetName();
}

void CConfigMain::OnConfig(wxCommandEvent&)
{
	if (g_video_backend)
		g_video_backend->ShowConfig(this);
}

// Change from IPL.LNG value to IPL.SADR country code
inline u8 CConfigMain::GetSADRCountryCode(int language)
{
	//http://wiibrew.org/wiki/Country_Codes
	u8 countrycode = language;
	switch (countrycode)
	{
	case 0: //Japanese
		countrycode = 1; //Japan
		break;
	case 1: //English
		countrycode = 49; //USA
		break;
	case 2: //German
		countrycode = 78; //Germany
		break;
	case 3: //French
		countrycode = 77; //France
		break;
	case 4: //Spanish
		countrycode = 105; //Spain
		break;
	case 5: //Italian
		countrycode = 83; //Italy
		break;
	case 6: //Dutch
		countrycode = 94; //Netherlands
		break;
	case 7: //Simplified Chinese
	case 8: //Traditional Chinese
		countrycode = 157; //China
		break;
	case 9: //Korean
		countrycode = 136; //Korea
		break;
	}
	return countrycode;
}
