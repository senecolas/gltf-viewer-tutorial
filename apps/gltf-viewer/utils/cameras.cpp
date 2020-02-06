#include "cameras.hpp"
#include "glfw.hpp"

#include <iostream>

// Good reference here to map camera movements to lookAt calls
// http://learnwebgl.brown37.net/07_cameras/camera_movement.html

using namespace glm;

struct ViewFrame
{
  vec3 left;
  vec3 up;
  vec3 front;
  vec3 eye;

  ViewFrame(vec3 l, vec3 u, vec3 f, vec3 e) : left(l), up(u), front(f), eye(e)
  {
  }
};

ViewFrame fromViewToWorldMatrix(const mat4 &viewToWorldMatrix)
{
  return ViewFrame{-vec3(viewToWorldMatrix[0]), vec3(viewToWorldMatrix[1]),
      -vec3(viewToWorldMatrix[2]), vec3(viewToWorldMatrix[3])};
}

bool FirstPersonCameraController::update(float elapsedTime)
{
  if (glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_LEFT) &&
      !m_LeftButtonPressed) {
    m_LeftButtonPressed = true;
    glfwGetCursorPos(
        m_pWindow, &m_LastCursorPosition.x, &m_LastCursorPosition.y);
  } else if (!glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_LEFT) &&
             m_LeftButtonPressed) {
    m_LeftButtonPressed = false;
  }

  const auto cursorDelta = ([&]() {
    if (m_LeftButtonPressed) {
      dvec2 cursorPosition;
      glfwGetCursorPos(m_pWindow, &cursorPosition.x, &cursorPosition.y);
      const auto delta = cursorPosition - m_LastCursorPosition;
      m_LastCursorPosition = cursorPosition;
      return delta;
    }
    return dvec2(0);
  })();

  float truckLeft = 0.f;
  float pedestalUp = 0.f;
  float dollyIn = 0.f;
  float rollRightAngle = 0.f;

  if (glfwGetKey(m_pWindow, GLFW_KEY_W)) {
    dollyIn += m_fSpeed * elapsedTime;
  }

  // Truck left
  if (glfwGetKey(m_pWindow, GLFW_KEY_A)) {
    truckLeft += m_fSpeed * elapsedTime;
  }

  // Pedestal up
  if (glfwGetKey(m_pWindow, GLFW_KEY_UP)) {
    pedestalUp += m_fSpeed * elapsedTime;
  }

  // Dolly out
  if (glfwGetKey(m_pWindow, GLFW_KEY_S)) {
    dollyIn -= m_fSpeed * elapsedTime;
  }

  // Truck right
  if (glfwGetKey(m_pWindow, GLFW_KEY_D)) {
    truckLeft -= m_fSpeed * elapsedTime;
  }

  // Pedestal down
  if (glfwGetKey(m_pWindow, GLFW_KEY_DOWN)) {
    pedestalUp -= m_fSpeed * elapsedTime;
  }

  if (glfwGetKey(m_pWindow, GLFW_KEY_Q)) {
    rollRightAngle -= 0.001f;
  }
  if (glfwGetKey(m_pWindow, GLFW_KEY_E)) {
    rollRightAngle += 0.001f;
  }

  // cursor going right, so minus because we want pan left angle:
  const float panLeftAngle = -0.01f * float(cursorDelta.x);
  const float tiltDownAngle = 0.01f * float(cursorDelta.y);

  const auto hasMoved = truckLeft || pedestalUp || dollyIn || panLeftAngle ||
                        tiltDownAngle || rollRightAngle;
  if (!hasMoved) {
    return false;
  }

  m_camera.moveLocal(truckLeft, pedestalUp, dollyIn);
  m_camera.rotateLocal(rollRightAngle, tiltDownAngle, 0.f);
  m_camera.rotateWorld(panLeftAngle, m_worldUpAxis);

  return true;
}

bool TrackballCameraController::update(float elapsedTime)
{
  if (glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_MIDDLE) &&
      !m_MiddleButtonPressed) {
    m_MiddleButtonPressed = true;
    glfwGetCursorPos(
        m_pWindow, &m_LastCursorPosition.x, &m_LastCursorPosition.y);
  } else if (!glfwGetMouseButton(m_pWindow, GLFW_MOUSE_BUTTON_MIDDLE) &&
             m_MiddleButtonPressed) {
    m_MiddleButtonPressed = false;
  }

  const auto cursorDelta = ([&]() {
    if (m_MiddleButtonPressed) {
      dvec2 cursorPosition;
      glfwGetCursorPos(m_pWindow, &cursorPosition.x, &cursorPosition.y);
      const auto delta = cursorPosition - m_LastCursorPosition;
      m_LastCursorPosition = cursorPosition;
      return delta;
    }
    return dvec2(0);
  })();

  float pedestalUp = 0.f;
  float dollyIn = 0.f;
  float rollRightAngle = 0.f;

  auto horizontalMovement = 0.01f * float(cursorDelta.x);
  auto verticalMovement = 0.01f * float(cursorDelta.y);
  const auto hasMoved = horizontalMovement || verticalMovement;
  if (!hasMoved) {
    return false;
  }

  // SHIFT = Pan
  if (glfwGetKey(m_pWindow, GLFW_KEY_LEFT_SHIFT)) {
    m_camera.moveLocal(horizontalMovement, pedestalUp, dollyIn);
    return true;
  }

  // CTRL = Zoom
  if (glfwGetKey(m_pWindow, GLFW_KEY_LEFT_CONTROL)) {
    if (horizontalMovement == 0.f) {
      return false;
    }

    const auto viewVector = m_camera.center() - m_camera.eye();
    const auto viewLength = glm::length(viewVector);

    if (horizontalMovement > 0.f) {
      // [FROM CHEAT] We don't want to move more that the length of the view vector (cannot go beyond target)
      horizontalMovement = glm::min(horizontalMovement, viewLength - 1e-4f);
    }

    // Normalize view vector for the translation
    const auto front = viewVector / viewLength;
    const auto translationVector = horizontalMovement * front;

    // Update camera
    const auto newEye = m_camera.eye() + translationVector;
    m_camera = Camera(newEye, m_camera.center(), m_worldUpAxis);

    return true;
  }

  // NO KEY = Rotate
  const auto depthAxis = m_camera.eye() - m_camera.center();

  // Vertical
  const auto horizontalAxis = m_camera.left();
  const auto longitudeRotationMatrix = rotate(mat4(1), verticalMovement, horizontalAxis);
  auto rotatedDepthAxis = vec3(longitudeRotationMatrix * vec4(depthAxis, 0));

  // Horizontal
  const auto latitudeRotationMatrix = rotate(mat4(1), -horizontalMovement, m_worldUpAxis);

  const auto result = vec3(latitudeRotationMatrix * vec4(rotatedDepthAxis, 0));

  // Update camera
  const auto newEye = m_camera.center() + result;
  m_camera = Camera(newEye, m_camera.center(), m_worldUpAxis);

  return true;
}