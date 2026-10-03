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

protected:
	ControllerState raw_state() override;

private:
	// Mouse-to-gyro sensitivity: radians/sec of simulated angular velocity
	// per pixel/sec of raw mouse movement. Tuned by feel (no real-world
	// equivalent to calibrate against, unlike a true gyro) - matches a
	// moderate, usable default; Xapfish's own page notes configurable
	// sensitivity as something it never got around to either, so this
	// starts as a fixed constant rather than a half-finished settings UI.
	static constexpr float kMouseGyroSensitivity = 0.0045f;

	WiiUMotionHandler m_motion_handler;
	std::chrono::steady_clock::time_point m_last_poll_time{};
	bool m_has_last_poll_time = false;
};
