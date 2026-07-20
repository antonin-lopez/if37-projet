#include "GaitAlgorithms.h"
#include <algorithm>
#include <cmath>

std::optional<float> ImpactDetector::processSample(float currentSample, uint32_t nowMs)
{
    if (!isInsideImpact_ && (nowMs - lastImpactEndMs_) < REFRACTORY_PERIOD_MS)
        return std::nullopt;

    if (!isInsideImpact_)
    {
        if (currentSample > thresholdG_)
        {
            isInsideImpact_ = true;
            currentPeak_ = currentSample;
        }
    }
    else
    {
        currentPeak_ = std::max(currentPeak_, currentSample);

        if (currentSample <= thresholdG_)
        {
            isInsideImpact_ = false;
            lastImpactEndMs_ = nowMs;
            return currentPeak_;
        }
    }

    return std::nullopt;
}

void GaitAnalyzer::reset()
{
    leftStepCount_ = rightStepCount_ = 0;
    leftAccumulator_ = rightAccumulator_ = 0.0f;

    leftBufferIdx_ = rightBufferIdx_ = 0;
    leftBufferCount_ = rightBufferCount_ = 0;
    for (uint8_t i = 0; i < RUNNING_WINDOW_SIZE; ++i)
    {
        leftBuffer_[i] = 0.0f;
        rightBuffer_[i] = 0.0f;
    }

    personalizedAsymmetryThreshold_ = ASYMMETRY_ALERT_MARGIN_PERCENT;
}

bool GaitAnalyzer::addCalibrationStep(float force, bool isLeft)
{
    if (isLeft && leftStepCount_ < CALIBRATION_STEPS_PER_SIDE)
    {
        leftAccumulator_ += force;
        leftStepCount_++;
        addRunningStep(force, isLeft);
    }
    else if (!isLeft && rightStepCount_ < CALIBRATION_STEPS_PER_SIDE)
    {
        rightAccumulator_ += force;
        rightStepCount_++;
        addRunningStep(force, isLeft);
    }

    if (leftStepCount_ >= CALIBRATION_STEPS_PER_SIDE && rightStepCount_ >= CALIBRATION_STEPS_PER_SIDE)
    {
        float avgLeftCalib = leftAccumulator_ / static_cast<float>(CALIBRATION_STEPS_PER_SIDE);
        float avgRightCalib = rightAccumulator_ / static_cast<float>(CALIBRATION_STEPS_PER_SIDE);

        float baselineAsymmetry = computeAsymmetry(avgLeftCalib, avgRightCalib);
        // Personal alert threshold = the runner's own baseline asymmetry,
        // plus a fixed margin, so alerts trigger on deviation from *their*
        // normal, not an arbitrary absolute value.
        personalizedAsymmetryThreshold_ = baselineAsymmetry + ASYMMETRY_ALERT_MARGIN_PERCENT;

        return true;
    }

    return false;
}

void GaitAnalyzer::addRunningStep(float force, bool isLeft)
{
    if (isLeft)
    {
        leftBuffer_[leftBufferIdx_] = force;
        leftBufferIdx_ = (leftBufferIdx_ + 1) % RUNNING_WINDOW_SIZE;
        if (leftBufferCount_ < RUNNING_WINDOW_SIZE)
            leftBufferCount_++;
    }
    else
    {
        rightBuffer_[rightBufferIdx_] = force;
        rightBufferIdx_ = (rightBufferIdx_ + 1) % RUNNING_WINDOW_SIZE;
        if (rightBufferCount_ < RUNNING_WINDOW_SIZE)
            rightBufferCount_++;
    }
}

float GaitAnalyzer::getLeftAverage() const
{
    if (leftBufferCount_ == 0)
        return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < leftBufferCount_; ++i)
        sum += leftBuffer_[i];
    return sum / leftBufferCount_;
}

float GaitAnalyzer::getRightAverage() const
{
    if (rightBufferCount_ == 0)
        return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < rightBufferCount_; ++i)
        sum += rightBuffer_[i];
    return sum / rightBufferCount_;
}

float GaitAnalyzer::computeAsymmetry(float left, float right) const
{
    if (left < minForceThreshold_ || right < minForceThreshold_)
        return 0.0f;
    return (std::abs(left - right) / std::max(left, right)) * 100.0f;
}
