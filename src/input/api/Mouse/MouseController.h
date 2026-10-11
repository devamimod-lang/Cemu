#pragma once

#include "input/api/Mouse/MouseControllerProvider.h"
#include "input/api/Controller.h"
#include "input/motion/MotionHandler.h"

#include <chrono>

class MouseController : public Controller<MouseControllerProvider>
{
public:
	MouseController();

	std::string_view api_name() const override
	{
		static_assert(to_string(InputAPI::Mouse) == "Mouse");
		return to_string(InputAPI::Mouse);
	}
	InputAPI::Type api() const override { return InputAPI::Mouse; }

	bool is_connected() override { return true; }

	bool has_axis() const override { return false; }

	bool has_motion() override { return true; }
	MotionSample get_motion_sample() override;

	std::string get_button_name(uint64 button) const override;

	// Faro Mouse motion: these three live in the controller profile rather
	// than in ControllerBase::Settings because they are meaningless for every
	// other provider (a real gyro needs no sensitivity scale - it reports
	// actual angular velocity). Persisted through the api-specific
	// save/load hook InputManager already calls for every controller.
	void save(pugi::xml_node& node) override;
	void load(const pugi::xml_node& node) override;

	// Radians/sec of simulated angular velocity per pixel/sec of raw mouse
	// movement. There is no real-world quantity to calibrate this against
	// (unlike a true gyro), and the usable range depends on the player's own
	// mouse DPI - which is exactly why it has to be adjustable instead of a
	// fixed constant: the same constant that feels right on an 800-DPI mouse
	// is roughly 5x too fast on a 4000-DPI one, which reads as "the mouse
	// gyro is broken on my PC".
	static constexpr float kSensitivityDefault = 0.0045f;
	static constexpr float kSensitivityMin = 0.0002f;
	static constexpr float kSensitivityMax = 0.02f;

	float get_sensitivity() const { return m_sensitivity; }
	void set_sensitivity(float sensitivity) { m_sensitivity = std::clamp(sensitivity, kSensitivityMin, kSensitivityMax); }
	bool get_invert_x() const { return m_invert_x; }
	void set_invert_x(bool invert) { m_invert_x = invert; }
	bool get_invert_y() const { return m_invert_y; }
	void set_invert_y(bool invert) { m_invert_y = invert; }

protected:
	ControllerState raw_state() override;

private:
	WiiUMotionHandler m_motion_handler;
	std::chrono::steady_clock::time_point m_last_poll_time{};
	bool m_has_last_poll_time = false;

	float m_sensitivity = kSensitivityDefault;
	bool m_invert_x = false;
	bool m_invert_y = false;
};
