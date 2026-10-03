#include <boost/container/small_vector.hpp>

#include "input/api/Mouse/MouseController.h"
#include "WindowSystem.h"

MouseController::MouseController()
	: base_type("mouse", "Mouse")
{
}

std::string MouseController::get_button_name(uint64 button) const
{
	switch (button)
	{
	case 0: return "Left Click";
	case 1: return "Right Click";
	case 2: return "Middle Click";
	case 3: return "Mouse Button 4";
	case 4: return "Mouse Button 5";
	default: return fmt::format("Mouse Button {}", button);
	}
}

ControllerState MouseController::raw_state()
{
	ControllerState result{};

	auto& windowInfo = WindowSystem::GetWindowInfo();
	if (windowInfo.debugger_focused)
		return result;

	boost::container::small_vector<uint32, 8> pressedButtons;
	windowInfo.iter_mouse_buttons([&pressedButtons](const std::pair<const uint32, bool>& buttonState) { if (buttonState.second) pressedButtons.emplace_back(buttonState.first); });
	result.buttons.SetPressedButtons(pressedButtons);

	// Faro Mouse motion: drain the raw mouse delta accumulated since the
	// last poll (see MainWindow's own WM_INPUT handling) and feed it into
	// the SAME WiiUMotionHandler pipeline DSU/SDL/Wiimote controllers use
	// (see input/motion/MotionHandler.h) - see Xapfish (CorySanin/Cemu),
	// the fork this feature is modeled on.
	//
	// Total integrated rotation ends up proportional to total accumulated
	// mouse movement regardless of polling rate: dividing the pixel delta
	// by deltaTime to get gx/gy (angular velocity), then having
	// WiiUMotionHandler integrate gx * deltaTime internally, cancels
	// deltaTime back out - so this stays frame-rate independent without
	// needing to special-case the polling interval.
	//
	// Axis mapping (horizontal mouse -> gz/yaw, vertical mouse -> gx/pitch)
	// is a first-pass choice based on the Wii U gamepad's own IMU axis
	// convention (see MotionSample.h's own captured-attitude comments) -
	// confirmed via research only as "the standard FPS mouselook mapping
	// applied to this console's axis layout", not via in-game testing
	// (that needs a human actually playing a motion-aimed game like
	// Splatoon) - may need gx/gz swapped or sign-flipped once tested.
	sint32 dx, dy;
	windowInfo.consume_mouse_delta(dx, dy);

	const auto now = std::chrono::steady_clock::now();
	if (m_has_last_poll_time)
	{
		float deltaTime = std::chrono::duration<float>(now - m_last_poll_time).count();
		if (deltaTime > 0.0001f && (dx != 0 || dy != 0))
		{
			float gz = (float)dx * kMouseGyroSensitivity / deltaTime;
			float gx = (float)dy * kMouseGyroSensitivity / deltaTime;
			m_motion_handler.processMotionSample(deltaTime, gx, 0.0f, gz, 0.0f, 0.0f, 1.0f);
		}
		else if (deltaTime > 0.0001f)
		{
			// No movement this poll - still integrate a zero-gyro sample so
			// the Mahony filter's own internal timing/bias tracking stays
			// consistent (matches how SDL/DSU always call
			// processMotionSample on every received sample, moved or not).
			m_motion_handler.processMotionSample(deltaTime, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
		}
	}
	m_last_poll_time = now;
	m_has_last_poll_time = true;

	return result;
}

MotionSample MouseController::get_motion_sample()
{
	return m_motion_handler.getMotionSample();
}
