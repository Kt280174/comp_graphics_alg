#pragma once
#include "Common.h"

class Camera
{
private:
    float m_yaw;
    float m_pitch;
    float m_distance;
    float m_moveSpeed;

public:
    Camera();
    void Update(float deltaTime, bool left, bool right, bool up, bool down);
    XMMATRIX GetViewMatrix() const;
    XMMATRIX GetViewNoTranslationMatrix() const;
    XMVECTOR GetEyePosition() const;
};