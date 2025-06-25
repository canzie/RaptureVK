#include "PerspectiveCamera.h"

#include "Logging/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Rapture
{
	PerspectiveCamera::PerspectiveCamera(float fov, float aspect_ratio, float near_plane, float far_plane)
	{
		m_viewMatrix = glm::mat4(1.0f);
		updateProjectionMatrix(fov, aspect_ratio, near_plane, far_plane);
	}


	void PerspectiveCamera::updateProjectionMatrix(float fov, float ratio, float nearPlane, float farPlane)
	{
		m_projectionMatrix = glm::perspective(glm::radians(fov), ratio, nearPlane, farPlane);
	}

	void PerspectiveCamera::updateViewMatrix(glm::vec3 translation)
	{
		m_viewMatrix = glm::translate(glm::mat4(1.0f), translation);
	}
	
	void PerspectiveCamera::updateViewMatrix(glm::vec3 translation, glm::vec3 cameraFront)
	{
		m_viewMatrix = glm::lookAt(translation, cameraFront + translation, glm::vec3(0.0f, 1.0f, 0.0f));

	}
	

}

