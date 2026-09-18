#pragma once

#include "../../app/streaming/video/ffmpeg-renderers/pacer/vrr/vrrtimingcontroller.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>

struct VrrReplayWorkerParameters {
    size_t queueCapacity = 3;
};

struct VrrReplayDisplayParameters {
    // Readiness remains false until the complete display model (including
    // transport and uncertainty assumptions) has been independently
    // measured or otherwise validated. Replay remains sweepable when 0.
    // Confirmation requires an explicit period and active duration, but
    // either the microsecond compatibility values or exact picosecond values
    // can satisfy that structural acknowledgement.
    unsigned int calibrationConfirmed = 0;
    // Zero uses the display period recorded in the trace.
    uint64_t scanoutPeriodUs = 0;
    // Optional picosecond period for raster classification and the propagated
    // counterfactual refresh clock. Replay derives it from the captured
    // physical-signal rational when omitted. The finer unit avoids cumulative
    // phase error from rounding a fractional-microsecond refresh period.
    uint64_t scanoutPeriodPs = 0;
    // Zero uses activeScanoutPercent of the resolved scanout period.
    uint64_t activeScanoutUs = 0;
    // Zero derives the exact value from the captured physical signal when
    // available, then falls back to activeScanoutUs/percent.
    uint64_t activeScanoutPs = 0;
    unsigned int activeScanoutPercent = 95;
    // Delay from DXGI's SyncQPCTime/SyncRefreshCount clock marker to the
    // modeled first active scan line. DXGI does not report this panel timing.
    uint64_t syncToActiveScanoutUs = 0;
    // Delay from the recorded CPU Present boundary to the modeled display
    // transition. This is calibration, not measured by schema 5.
    uint64_t presentTransportUs = 0;
    // Samples this close to a scanout boundary are classified as uncertain.
    uint64_t phaseUncertaintyUs = 250;
    // Zero derives a per-frame limit from the display and source periods.
    uint64_t anchorMaxAgeUs = 0;
};

struct VrrReplayExecutionParameters {
    // Zero-default deterministic fault injection. These effects apply only to
    // the candidate timeline and make later anomaly sweeps reproducible.
    uint64_t decisionDelayUs = 0;
    // Extra scheduler wake lateness. Unlike preparation/submission work,
    // these values also feed the controller's learned scheduler budgets.
    uint64_t renderWakeDelayUs = 0;
    uint64_t targetWakeDelayUs = 0;
    uint64_t preparationDelayUs = 0;
    uint64_t submissionDelayUs = 0;
    // Extra delay after the candidate CPU Present boundary. This moves only
    // the modeled display transition, leaving candidate submission latency
    // and controller learning unchanged.
    uint64_t displayTransitionDelayUs = 0;
    // Synthetic second-check deficit used to exercise adaptive guard
    // learning without corrupting the captured reference lifecycle.
    uint64_t spacingGuardFeedbackUs = 0;
    // Deliberately bypasses the target/spacing floor, but never buffer
    // readiness, to model an early-Present fault.
    uint64_t submissionAdvanceUs = 0;
    // Alignment captures issue an observation-only D3DKMT scan-line query
    // immediately before Present. When enabled, remove the exact measured
    // query duration from each candidate submission residual without crossing
    // the reconstructed target/spacing/readiness floor.
    unsigned int removePrePresentRasterProbeOverhead = 0;
    uint64_t periodicStallEveryFrames = 0;
    uint64_t periodicStallPhaseFrames = 0;
    uint64_t periodicStallBurstFrames = 1;
    // periodicStallUs is the decision-stage stall retained for compatibility.
    uint64_t periodicStallUs = 0;
    uint64_t periodicRenderWakeDelayUs = 0;
    uint64_t periodicTargetWakeDelayUs = 0;
    uint64_t periodicPreparationStallUs = 0;
    uint64_t periodicSubmissionStallUs = 0;
    uint64_t periodicDisplayTransitionDelayUs = 0;
    uint64_t periodicSpacingGuardFeedbackUs = 0;
    uint64_t periodicSubmissionAdvanceUs = 0;
};

struct VrrReplayAssertion {
    QString metric;
    QString operation;
    double value = 0;
};

struct VrrReplayScenario {
    QString name = "candidate";
    QString mode = "fixed";
    VrrTimingParameters controller;
    bool controllerCustomized = false;
    VrrReplayWorkerParameters worker;
    VrrReplayDisplayParameters display;
    VrrReplayExecutionParameters execution;
    QList<VrrReplayAssertion> assertions;
};

struct VrrReplayConfiguration {
    VrrTimingParameters commonController;
    bool commonControllerCustomized = false;
    VrrReplayWorkerParameters commonWorker;
    VrrReplayDisplayParameters commonDisplay;
    VrrReplayExecutionParameters commonExecution;
    QList<VrrReplayScenario> scenarios;
};

QJsonObject vrrTimingParametersToJson(const VrrTimingParameters& value);
QJsonObject vrrWorkerParametersToJson(const VrrReplayWorkerParameters& value);
QJsonObject vrrDisplayParametersToJson(const VrrReplayDisplayParameters& value);
QJsonObject vrrExecutionParametersToJson(
    const VrrReplayExecutionParameters& value);
QJsonObject vrrDefaultReplayConfigurationJson();
QStringList vrrReplayParameterNames();

bool loadVrrReplayConfiguration(const QByteArray& json,
                                VrrReplayConfiguration& configuration,
                                QString& error);
bool applyVrrReplayOverride(const QString& expression,
                            VrrReplayScenario& scenario,
                            QString& error);
bool applyVrrReplayControllerSnapshot(const QJsonObject& object,
                                      VrrTimingParameters& parameters,
                                      QString& error);
bool validateVrrTimingParameters(const VrrTimingParameters& value,
                                 QString& error);
bool validateVrrWorkerParameters(const VrrReplayWorkerParameters& value,
                                 QString& error);
bool validateVrrDisplayParameters(const VrrReplayDisplayParameters& value,
                                  QString& error);
bool validateVrrExecutionParameters(const VrrReplayExecutionParameters& value,
                                    QString& error);
