#include "TestLayer.h"

#include "logging/Log.h"
#include "window_context/Application.h"
#include "window_context/Telemetry.h"

static constexpr float REPORT_INTERVAL = 1.0f;
static constexpr double BYTES_PER_MB = 1024.0 * 1024.0;

TestLayer::TestLayer() : Layer("Test Layer") {}

void TestLayer::onAttach()
{
    m_frameCount = 0;
    m_elapsed = 0.0f;
}

void TestLayer::onDetach() {}

void TestLayer::onUpdate(float ts)
{
    m_frameCount++;
    m_elapsed += ts;

    if (m_elapsed < REPORT_INTERVAL) {
        return;
    }

    float fps = static_cast<float>(m_frameCount) / m_elapsed;

    const Rapture::Telemetry &telemetry = Rapture::Application::getInstance().getTelemetry();
    double vramUsedMb = static_cast<double>(telemetry.vramUsedBytes) / BYTES_PER_MB;
    double vramBudgetMb = static_cast<double>(telemetry.vramBudgetBytes) / BYTES_PER_MB;
    double vramPercent = telemetry.vramBudgetBytes > 0 ? (vramUsedMb / vramBudgetMb) * 100.0 : 0.0;
    double ramUsedMb = static_cast<double>(telemetry.ramUsedBytes) / BYTES_PER_MB;

    RP_INFO("FPS: {0:.1f}  VRAM: {1:.0f}/{2:.0f} MB ({3:.1f}%)  RAM: {4:.0f} MB", fps, vramUsedMb, vramBudgetMb, vramPercent,
            ramUsedMb);

    m_frameCount = 0;
    m_elapsed = 0.0f;
}
