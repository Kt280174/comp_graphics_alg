#include "Camera.h"

Camera::Camera() : m_yaw(0.0f), m_pitch(0.3f), m_distance(3.0f), m_moveSpeed(1.0f) {}

void Camera::Update(float deltaTime, bool left, bool right, bool up, bool down)
{
    if (left) m_yaw -= m_moveSpeed * deltaTime;
    if (right) m_yaw += m_moveSpeed * deltaTime;
    if (up) m_pitch += m_moveSpeed * deltaTime;
    if (down) m_pitch -= m_moveSpeed * deltaTime;

    const float maxPitch = 1.5f;
    if (m_pitch > maxPitch) m_pitch = maxPitch;
    if (m_pitch < -maxPitch) m_pitch = -maxPitch;
}

XMMATRIX Camera::GetViewMatrix() const
{
    float camX = m_distance * sin(m_yaw) * cos(m_pitch);
    float camY = m_distance * sin(m_pitch);
    float camZ = m_distance * cos(m_yaw) * cos(m_pitch);

    XMVECTOR eye = XMVectorSet(camX, camY, camZ, 0.0f);
    XMVECTOR at = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    return XMMatrixLookAtLH(eye, at, up);
}

XMMATRIX Camera::GetViewNoTranslationMatrix() const
{
    XMMATRIX view = GetViewMatrix();
    view.r[3] = XMVectorSet(0, 0, 0, 1);
    return view;
}

XMVECTOR Camera::GetEyePosition() const
{
    return XMVectorSet(
        m_distance * sin(m_yaw) * cos(m_pitch),
        m_distance * sin(m_pitch),
        m_distance * cos(m_yaw) * cos(m_pitch),
        0.0f
    );
}