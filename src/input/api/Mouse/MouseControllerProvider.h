#pragma once

#include "input/api/ControllerProvider.h"

#ifndef HAS_MOUSE
#define HAS_MOUSE 1
#endif

// Faro: mouse-to-gyro motion input, inspired by the Xapfish Cemu fork
// (CorySanin/Cemu) - translates raw (relative) mouse movement into
// simulated Wii U gyro angular velocity, feeding the SAME WiiUMotionHandler
// pipeline the DSU/SDL/Wiimote providers already use (see
// input/motion/MotionHandler.h). Useful for motion-aimed games like
// Splatoon when a real gyro-capable controller isn't available. Mirrors
// KeyboardControllerProvider's shape (single, always-available "device" -
// the system mouse - no settings/discovery needed).
class MouseControllerProvider : public ControllerProviderBase
{
	friend class MouseController;
public:
	inline static InputAPI::Type kAPIType = InputAPI::Mouse;
	InputAPI::Type api() const override { return kAPIType; }

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;
};
