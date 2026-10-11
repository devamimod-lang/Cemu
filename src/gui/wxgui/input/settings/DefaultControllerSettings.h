#pragma once

#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/slider.h>

#include "input/api/Controller.h"
#include "input/api/Mouse/MouseController.h"

class wxCheckBox;
class wxInputDraw;

class DefaultControllerSettings : public wxDialog
{
public:
	DefaultControllerSettings(wxWindow* parent, const wxPoint& position, ControllerPtr controller);
	~DefaultControllerSettings();

private:
	void update_settings();

	ControllerPtr m_controller;
	ControllerBase::Settings m_settings;
	float m_rumble_backup;

	wxTimer* m_timer;
	std::optional<std::chrono::steady_clock::time_point> m_rumble_time{};

	wxSlider* m_axis_deadzone, *m_axis_range;
	wxSlider* m_rotation_deadzone, *m_rotation_range;
	wxSlider* m_trigger_deadzone, *m_trigger_range;
	wxSlider* m_rumble;

	wxCheckBox* m_use_motion = nullptr;

	// Faro Mouse motion: non-null only when this dialog was opened for the
	// Mouse provider; the controls below exist only in that case. See the
	// constructor's own "Mouse motion" box for why a simulated gyro needs
	// these and a real one does not.
	std::shared_ptr<MouseController> m_mouse_controller;
	wxSlider* m_mouse_sensitivity = nullptr;
	wxCheckBox* m_mouse_invert_x = nullptr;
	wxCheckBox* m_mouse_invert_y = nullptr;

	wxInputDraw* m_axis_draw, * m_rotation_draw, *m_trigger_draw;

	void on_timer(wxTimerEvent& event);
	void on_close(wxCloseEvent& event);
	void on_deadzone_change(wxCommandEvent& event);
	void on_range_change(wxCommandEvent& event);
	void on_rumble_change(wxCommandEvent& event);
	void on_mouse_sensitivity_change(wxCommandEvent& event);

	static float slider_to_sensitivity(int value);
	static int sensitivity_to_slider(float sensitivity);
};
