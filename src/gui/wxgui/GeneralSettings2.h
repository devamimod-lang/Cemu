#pragma once
#include "config/CemuConfig.h"
#include <wx/collpane.h>
#include <wx/propgrid/propgrid.h>
#include <Cafe/Account/Account.h>

class wxCheckBox;
class wxChoice;
class wxColourPickerCtrl;
class wxListBox;
class wxBookCtrlBase;
class wxRadioBox;
class wxSlider;
class wxSpinCtrl;
class wxSpinCtrlDouble;
class wxStaticText;

wxDECLARE_EVENT(wxEVT_ACCOUNTLIST_REFRESH, wxCommandEvent);

class GeneralSettings2 : public wxDialog
{
public:
	GeneralSettings2(wxWindow* parent, bool game_launched);
	~GeneralSettings2();

	[[nodiscard]] bool ShouldReloadGamelist() const  { return m_reload_gamelist; }
	[[nodiscard]] bool MLCModified() const  { return m_mlc_modified; }
	void OnClose(wxCloseEvent& event);

private:
	void ValidateConfig();
	void StoreConfig();
	void DisableSettings(bool game_launched);

	bool m_reload_gamelist = false;
	bool m_mlc_modified = false;
	bool m_game_launched;

	bool m_has_account_change = false; // keep track of dirty state of accounts
	std::vector<GraphicAPI> m_api_map; // map from dropdown index to GraphicsAPISetting, used in HandleGraphicsApiSelection


	wxPanel* AddGeneralPage(wxBookCtrlBase* notebook);
	wxPanel* AddGraphicsPage(wxBookCtrlBase* notebook);
	wxPanel* AddAudioPage(wxBookCtrlBase* notebook);
	wxPanel* AddOverlayPage(wxBookCtrlBase* notebook);
	wxPanel* AddAccountPage(wxBookCtrlBase* notebook);
	wxPanel* AddDebugPage(wxBookCtrlBase* notebook);

	// General
	wxChoice * m_language;
	wxCheckBox* m_save_window_position_size;
	wxCheckBox* m_save_padwindow_position_size;
	wxCheckBox* m_discord_presence, *m_fullscreen_menubar;
	wxCheckBox* m_auto_update, *m_receive_untested_releases, *m_save_screenshot;
	wxCheckBox* m_disable_screensaver;
	wxCheckBox* m_play_boot_sound;
#if BOOST_OS_WINDOWS
	wxChoice* m_msw_theme;
	wxChoice* m_msw_backdrop;
#endif
#if BOOST_OS_LINUX && defined(ENABLE_FERAL_GAMEMODE)
   	wxCheckBox* m_feral_gamemode;
#endif
	wxListBox* m_game_paths;
	wxTextCtrl* m_mlc_path;

	// Graphics
	wxChoice* m_graphic_api, * m_graphic_device;
	wxChoice* m_vsync;
	wxCheckBox* m_overrideGamma;
	wxSpinCtrlDouble* m_overrideGammaValue;
	wxSpinCtrlDouble* m_userDisplayGamma;
	wxCheckBox* m_userDisplayisSRGB;

	wxCheckBox *m_async_compile, *m_gx2drawdone_sync;
#ifdef ENABLE_METAL
	wxCheckBox *m_force_mesh_shaders;
#endif
	wxRadioBox* m_upscale_filter, *m_downscale_filter, *m_fullscreen_scaling;
	wxChoice* m_antialiasing_mode;
	wxChoice* m_smaa_quality;
	wxChoice* m_taa_spatial_aa;
	// Faro FSR1: RCAS controls - only enabled while FSR 1.0 is the active
	// upscale filter (RCAS is FSR1's own second pass).
	wxSlider* m_fsr_rcas_sharpness;
	// A member, not a local captured by the slider's handler: the settings
	// reload path has to refresh this label too, or the figure goes stale.
	wxStaticText* m_fsr_rcas_sharpness_text;
	wxCheckBox* m_fsr_rcas_denoise;
	// AMD's stored scale counts stops of sharpness REDUCTION, so it runs
	// backwards from how the control reads. This renders it as a sharpness
	// percentage that rises with the slider.
	static wxString FormatRcasSharpness(sint32 stops);
	// Faro: keeps a slider's numeric readout from resizing as the value changes.
	void SetSliderValueWidth(wxStaticText* text) const;
	wxRadioBox* m_force_anisotropic_level;
	wxCheckBox *m_faro_postfx, *m_faro_ssao;
	wxCheckBox* m_faro_depth_snapshot;
	wxChoice* m_faro_debug_view;
	wxSlider *m_faro_ssao_intensity;
	wxStaticText* m_faro_ssao_intensity_text;
	wxChoice* m_faro_ssao_quality;
	wxChoice* m_overlay_position, *m_notification_position, *m_overlay_scale, *m_notification_scale;
	wxCheckBox* m_controller_profile_name, *m_controller_low_battery, *m_shader_compiling, *m_friends_data;
	wxCheckBox *m_overlay_fps, *m_overlay_drawcalls, *m_overlay_cpu, *m_overlay_cpu_per_core,*m_overlay_ram, *m_overlay_vram, *m_overlay_debug;
	wxColourPickerCtrl *m_overlay_font_color, *m_notification_font_color;

	// Audio
	wxChoice* m_audio_api;
	wxSlider *m_audio_latency;
	wxSlider *m_tv_volume, *m_pad_volume, *m_input_volume, *m_portal_volume;
	wxChoice *m_tv_channels, *m_pad_channels, *m_input_channels;
	wxChoice *m_tv_device, *m_pad_device, *m_input_device, *m_portal_device;

	// Account
	wxButton* m_create_account, * m_delete_account;
	wxChoice* m_active_account;
	wxRadioBox* m_active_service;
	wxCollapsiblePane* m_account_information;
	wxPropertyGrid* m_account_grid;
	wxBitmapButton* m_validate_online;
	wxStaticText* m_online_status;

	// Debug
	wxChoice* m_crash_dump;
	wxSpinCtrl* m_gdb_port;
#ifdef ENABLE_METAL
	wxTextCtrl* m_gpu_capture_dir;
	wxCheckBox* m_framebuffer_fetch;
#endif

	void OnAccountCreate(wxCommandEvent& event);
	void OnAccountDelete(wxCommandEvent& event);
	void OnAccountSettingsChanged(wxPropertyGridEvent& event);
	void OnAudioLatencyChanged(wxCommandEvent& event);
	void OnVolumeChanged(wxCommandEvent& event);
	void OnInputVolumeChanged(wxCommandEvent& event);
	void OnSliderChangedPercent(wxCommandEvent& event);
	void OnLatencySliderChanged(wxCommandEvent& event);
	void OnAudioAPISelected(wxCommandEvent& event);
	void OnAudioDeviceSelected(wxCommandEvent& event);
	void OnAudioChannelsSelected(wxCommandEvent& event);
	void OnGraphicAPISelected(wxCommandEvent& event);
	void OnUserDisplaySRGBSelected(wxCommandEvent& event);
	void OnAddPathClicked(wxCommandEvent& event);
	void OnRemovePathClicked(wxCommandEvent& event);
	void OnActiveAccountChanged(wxCommandEvent& event);
	void OnMLCPathSelect(wxCommandEvent& event);
	void OnMLCPathClear(wxCommandEvent& event);
	void OnShowOnlineValidator(wxCommandEvent& event);
	void OnAccountServiceChanged(wxCommandEvent& event);
	static wxString GetOnlineAccountErrorMessage(OnlineAccountError error);

	uint32 GetSelectedAccountPersistentId();

	// updates cemu audio devices
	void UpdateAudioDevice();
	// refreshes audio device list for dropdown
	void UpdateAudioDeviceList();

	void ResetAccountInformation();
	void UpdateAccountInformation();
	void UpdateOnlineAccounts();
	void HandleGraphicsApiSelection();
	void ApplyConfig();
};
