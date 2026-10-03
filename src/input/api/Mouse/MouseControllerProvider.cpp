#include "input/api/Mouse/MouseControllerProvider.h"

#include "input/api/Mouse/MouseController.h"

std::vector<std::shared_ptr<ControllerBase>> MouseControllerProvider::get_controllers()
{
	std::vector<std::shared_ptr<ControllerBase>> result;
	result.emplace_back(std::make_shared<MouseController>());
	return result;
}
