// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cmath>
#include <numbers>

#include "common/math_util.h"
#include "hid_core/frontend/motion_input.h"

namespace Core::HID {

MotionInput::MotionInput() {
    // Initialize PID constants with default values
    SetPID(0.3f, 0.005f, 0.0f);
    SetGyroThreshold(ThresholdStandard);
    ResetQuaternion();
    ResetRotations();
}

void MotionInput::SetPID(f32 new_kp, f32 new_ki, f32 new_kd) {
    kp = new_kp;
    ki = new_ki;
    kd = new_kd;
}

void MotionInput::SetAcceleration(const Common::Vec<f32, 3>& acceleration) {
    accel = acceleration;
    accel[0] = std::clamp(accel[0], -AccelMaxValue, AccelMaxValue);
    accel[1] = std::clamp(accel[1], -AccelMaxValue, AccelMaxValue);
    accel[2] = std::clamp(accel[2], -AccelMaxValue, AccelMaxValue);
}

void MotionInput::SetGyroscope(const Common::Vec<f32, 3>& gyroscope) {
    gyro = gyroscope - gyro_bias;

    gyro[0] = std::clamp(gyro[0], -GyroMaxValue, GyroMaxValue);
    gyro[1] = std::clamp(gyro[1], -GyroMaxValue, GyroMaxValue);
    gyro[2] = std::clamp(gyro[2], -GyroMaxValue, GyroMaxValue);

    // Auto adjust gyro_bias to minimize drift
    if (!IsMoving(IsAtRestRelaxed)) {
        gyro_bias = (gyro_bias * 0.9999f) + (gyroscope * 0.0001f);
    }

    // Adjust drift when calibration mode is enabled
    if (calibration_mode) {
        gyro_bias = (gyro_bias * 0.99f) + (gyroscope * 0.01f);
        StopCalibration();
    }

    if (gyro.Length() < gyro_threshold * user_gyro_threshold) {
        gyro = {};
    } else {
        only_accelerometer = false;
    }
}

void MotionInput::SetQuaternion(const Common::Vec<f32, 4>& quaternion) {
    quat = quaternion;
}

void MotionInput::SetEulerAngles(const Common::Vec<f32, 3>& euler_angles) {
    const float cr = std::cos(euler_angles[0] * 0.5f);
    const float sr = std::sin(euler_angles[0] * 0.5f);
    const float cp = std::cos(euler_angles[1] * 0.5f);
    const float sp = std::sin(euler_angles[1] * 0.5f);
    const float cy = std::cos(euler_angles[2] * 0.5f);
    const float sy = std::sin(euler_angles[2] * 0.5f);

    quat[3] = cr * cp * cy + sr * sp * sy;
    quat[0] = sr * cp * cy - cr * sp * sy;
    quat[1] = cr * sp * cy + sr * cp * sy;
    quat[2] = cr * cp * sy - sr * sp * cy;
}

void MotionInput::SetGyroBias(const Common::Vec<f32, 3>& bias) {
    gyro_bias = bias;
}

void MotionInput::SetGyroThreshold(f32 threshold) {
    gyro_threshold = threshold;
}

void MotionInput::SetUserGyroThreshold(f32 threshold) {
    user_gyro_threshold = threshold / ThresholdStandard;
}

void MotionInput::EnableReset(bool reset) {
    reset_enabled = reset;
}

void MotionInput::ResetRotations() {
    rotations = {};
}

void MotionInput::ResetQuaternion() {
    quat = Common::Vec<f32, 4>{0.0f, 0.0f, -1.0f, 0.0f};
}

bool MotionInput::IsMoving(f32 sensitivity) const {
    return gyro.Length() >= sensitivity || accel.Length() <= 0.9f || accel.Length() >= 1.1f;
}

bool MotionInput::IsCalibrated(f32 sensitivity) const {
    return real_error.Length() < sensitivity;
}

void MotionInput::UpdateRotation(u64 elapsed_time) {
    const auto sample_period = static_cast<f32>(elapsed_time) / 1000000.0f;
    if (sample_period > 0.1f) {
        return;
    }
    rotations += gyro * sample_period;
}

void MotionInput::Calibrate() {
    calibration_mode = true;
    calibration_counter = 0;
}

void MotionInput::StopCalibration() {
    if (calibration_counter++ > CalibrationSamples) {
        calibration_mode = false;
        ResetQuaternion();
        ResetRotations();
    }
}

// Based on Madgwick's implementation of Mayhony's AHRS algorithm.
// https://github.com/xioTechnologies/Open-Source-AHRS-With-x-IMU/blob/master/x-IMU%20IMU%20and%20AHRS%20Algorithms/x-IMU%20IMU%20and%20AHRS%20Algorithms/AHRS/MahonyAHRS.cs
void MotionInput::UpdateOrientation(u64 elapsed_time) {
    if (!IsCalibrated(0.1f)) {
        ResetOrientation();
    }
    // Short name local variable for readability
    f32 q1 = quat[3];
    f32 q2 = quat[0];
    f32 q3 = quat[1];
    f32 q4 = quat[2];
    const auto sample_period = static_cast<f32>(elapsed_time) / 1000000.0f;

    // Ignore invalid elapsed time
    if (sample_period > 0.1f) {
        return;
    }

    const auto normal_accel = accel.Normalized();
    auto rad_gyro = gyro * std::numbers::pi_v<float> * 2.f;
    const f32 swap = rad_gyro[0];
    rad_gyro[0] = rad_gyro[1];
    rad_gyro[1] = -swap;
    rad_gyro[2] = -rad_gyro[2];

    // Clear gyro values if there is no gyro present
    if (only_accelerometer) {
        rad_gyro[0] = 0;
        rad_gyro[1] = 0;
        rad_gyro[2] = 0;
    }

    // Ignore drift correction if acceleration is not reliable
    if (accel.Length() >= 0.75f && accel.Length() <= 1.25f) {
        const f32 ax = -normal_accel[0];
        const f32 ay = normal_accel[1];
        const f32 az = -normal_accel[2];

        // Estimated direction of gravity
        const f32 vx = 2.0f * (q2 * q4 - q1 * q3);
        const f32 vy = 2.0f * (q1 * q2 + q3 * q4);
        const f32 vz = q1 * q1 - q2 * q2 - q3 * q3 + q4 * q4;

        // Error is cross product between estimated direction and measured direction of gravity
        const Common::Vec<f32, 3> new_real_error{
            az * vx - ax * vz,
            ay * vz - az * vy,
            ax * vy - ay * vx,
        };

        derivative_error = new_real_error - real_error;
        real_error = new_real_error;

        // Prevent integral windup
        if (ki != 0.0f && !IsCalibrated(0.05f)) {
            integral_error += real_error;
        } else {
            integral_error = {};
        }

        // Apply feedback terms
        if (!only_accelerometer) {
            rad_gyro += kp * real_error;
            rad_gyro += ki * integral_error;
            rad_gyro += kd * derivative_error;
        } else {
            // Give more weight to accelerometer values to compensate for the lack of gyro
            rad_gyro += 35.0f * kp * real_error;
            rad_gyro += 10.0f * ki * integral_error;
            rad_gyro += 10.0f * kd * derivative_error;

            // Emulate gyro values for games that need them
            gyro[0] = -rad_gyro[1];
            gyro[1] = rad_gyro[0];
            gyro[2] = -rad_gyro[2];
            UpdateRotation(elapsed_time);
        }
    }

    const f32 gx = rad_gyro[1];
    const f32 gy = rad_gyro[0];
    const f32 gz = rad_gyro[2];

    // Integrate rate of change of quaternion
    const f32 pa = q2;
    const f32 pb = q3;
    const f32 pc = q4;
    q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5f * sample_period);
    q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5f * sample_period);
    q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5f * sample_period);
    q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5f * sample_period);

    quat[3] = q1;
    quat[0] = q2;
    quat[1] = q3;
    quat[2] = q4;
    quat = quat.Normalized();
}

std::array<Common::Vec<f32, 3>, 3> MotionInput::GetOrientation() const {
    const Common::Vec<f32, 4> quad{
        -quat[1],
        -quat[0],
        -quat[3],
        -quat[2],
    };
    const std::array<f32, 16> matrix4x4 = quad.ToMatrix();
    return {Common::Vec<f32, 3>(matrix4x4[0], matrix4x4[1], -matrix4x4[2]),
            Common::Vec<f32, 3>(matrix4x4[4], matrix4x4[5], -matrix4x4[6]),
            Common::Vec<f32, 3>(-matrix4x4[8], -matrix4x4[9], matrix4x4[10])};
}

Common::Vec<f32, 3> MotionInput::GetAcceleration() const {
    return accel;
}

Common::Vec<f32, 3> MotionInput::GetGyroscope() const {
    return gyro;
}

Common::Vec<f32, 3> MotionInput::GetGyroBias() const {
    return gyro_bias;
}

Common::Vec<f32, 4> MotionInput::GetQuaternion() const {
    return quat;
}

Common::Vec<f32, 3> MotionInput::GetRotations() const {
    return rotations;
}

Common::Vec<f32, 3> MotionInput::GetEulerAngles() const {
    // roll (x-axis rotation)
    const float sinr_cosp = 2 * (quat[3] * quat[0] + quat[1] * quat[2]);
    const float cosr_cosp = 1 - 2 * (quat[0] * quat[0] + quat[1] * quat[1]);

    // pitch (y-axis rotation)
    const float sinp = std::sqrt(1 + 2 * (quat[3] * quat[1] - quat[0] * quat[2]));
    const float cosp = std::sqrt(1 - 2 * (quat[3] * quat[1] - quat[0] * quat[2]));

    // yaw (z-axis rotation)
    const float siny_cosp = 2 * (quat[3] * quat[2] + quat[0] * quat[1]);
    const float cosy_cosp = 1 - 2 * (quat[1] * quat[1] + quat[2] * quat[2]);

    return {
        std::atan2(sinr_cosp, cosr_cosp),
        2 * std::atan2(sinp, cosp) - float(std::numbers::pi_v<float>) / 2,
        std::atan2(siny_cosp, cosy_cosp),
    };
}

void MotionInput::ResetOrientation() {
    if (!reset_enabled || only_accelerometer) {
        return;
    }
    if (!IsMoving(IsAtRestRelaxed) && accel[2] <= -0.9f) {
        ++reset_counter;
        if (reset_counter > 900) {
            quat[3] = 0;
            quat[0] = 0;
            quat[1] = 0;
            quat[2] = -1;
            SetOrientationFromAccelerometer();
            integral_error = {};
            reset_counter = 0;
        }
    } else {
        reset_counter = 0;
    }
}

void MotionInput::SetOrientationFromAccelerometer() {
    int iterations = 0;
    const f32 sample_period = 0.015f;

    const auto normal_accel = accel.Normalized();

    while (!IsCalibrated(0.01f) && ++iterations < 100) {
        // Short name local variable for readability
        f32 q1 = quat[3];
        f32 q2 = quat[0];
        f32 q3 = quat[1];
        f32 q4 = quat[2];

        Common::Vec<f32, 3> rad_gyro;
        const f32 ax = -normal_accel[0];
        const f32 ay = normal_accel[1];
        const f32 az = -normal_accel[2];

        // Estimated direction of gravity
        const f32 vx = 2.0f * (q2 * q4 - q1 * q3);
        const f32 vy = 2.0f * (q1 * q2 + q3 * q4);
        const f32 vz = q1 * q1 - q2 * q2 - q3 * q3 + q4 * q4;

        // Error is cross product between estimated direction and measured direction of gravity
        const Common::Vec<f32, 3> new_real_error = {
            az * vx - ax * vz,
            ay * vz - az * vy,
            ax * vy - ay * vx,
        };

        derivative_error = new_real_error - real_error;
        real_error = new_real_error;

        rad_gyro += 10.0f * kp * real_error;
        rad_gyro += 5.0f * ki * integral_error;
        rad_gyro += 10.0f * kd * derivative_error;

        const f32 gx = rad_gyro[1];
        const f32 gy = rad_gyro[0];
        const f32 gz = rad_gyro[2];

        // Integrate rate of change of quaternion
        const f32 pa = q2;
        const f32 pb = q3;
        const f32 pc = q4;
        q1 = q1 + (-q2 * gx - q3 * gy - q4 * gz) * (0.5f * sample_period);
        q2 = pa + (q1 * gx + pb * gz - pc * gy) * (0.5f * sample_period);
        q3 = pb + (q1 * gy - pa * gz + pc * gx) * (0.5f * sample_period);
        q4 = pc + (q1 * gz + pa * gy - pb * gx) * (0.5f * sample_period);

        quat[3] = q1;
        quat[0] = q2;
        quat[1] = q3;
        quat[2] = q4;
        quat = quat.Normalized();
    }
}
} // namespace Core::HID
