/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/noise_level_estimator.h"

#include <array>
#include <functional>
#include <limits>

#include "api/function_view.h"
#include "modules/audio_processing/agc2/agc2_testing_common.h"
#include "modules/audio_processing/agc2/vector_float_frame.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/gunit.h"

namespace webrtc {
namespace {

constexpr int kNumIterations = 200;
constexpr int kFramesPerSecond = 100;

// Runs the noise estimator on audio generated by 'sample_generator'
// for kNumIterations. Returns the last noise level estimate.
float RunEstimator(rtc::FunctionView<float()> sample_generator,
                   int sample_rate_hz) {
  ApmDataDumper data_dumper(0);
  auto estimator = CreateNoiseLevelEstimator(&data_dumper);
  const int samples_per_channel =
      rtc::CheckedDivExact(sample_rate_hz, kFramesPerSecond);
  VectorFloatFrame signal(1, samples_per_channel, 0.0f);

  for (int i = 0; i < kNumIterations; ++i) {
    AudioFrameView<float> frame_view = signal.float_frame_view();
    for (int j = 0; j < samples_per_channel; ++j) {
      frame_view.channel(0)[j] = sample_generator();
    }
    estimator->Analyze(frame_view);
  }
  return estimator->Analyze(signal.float_frame_view());
}

class NoiseEstimatorParametrization : public ::testing::TestWithParam<int> {
 protected:
  int sample_rate_hz() const { return GetParam(); }
};

// White random noise is stationary, but does not trigger the detector
// every frame due to the randomness.
TEST_P(NoiseEstimatorParametrization, RandomNoise) {
  test::WhiteNoiseGenerator gen(/*min_amplitude=*/test::kMinS16,
                                /*max_amplitude=*/test::kMaxS16);
  const float noise_level_dbfs = RunEstimator(gen, sample_rate_hz());
  EXPECT_NEAR(noise_level_dbfs, -5.5f, 1.0f);
}

// Sine curves are (very) stationary. They trigger the detector all
// the time. Except for a few initial frames.
TEST_P(NoiseEstimatorParametrization, SineTone) {
  test::SineGenerator gen(/*amplitude=*/test::kMaxS16, /*frequency_hz=*/600.0f,
                          sample_rate_hz());
  const float noise_level_dbfs = RunEstimator(gen, sample_rate_hz());
  EXPECT_NEAR(noise_level_dbfs, -3.0f, 1.0f);
}

// Pulses are transient if they are far enough apart. They shouldn't
// trigger the noise detector.
TEST_P(NoiseEstimatorParametrization, PulseTone) {
  test::PulseGenerator gen(/*pulse_amplitude=*/test::kMaxS16,
                           /*no_pulse_amplitude=*/10.0f, /*frequency_hz=*/20.0f,
                           sample_rate_hz());
  const int noise_level_dbfs = RunEstimator(gen, sample_rate_hz());
  EXPECT_NEAR(noise_level_dbfs, -79.0f, 1.0f);
}

INSTANTIATE_TEST_SUITE_P(GainController2NoiseEstimator,
                         NoiseEstimatorParametrization,
                         ::testing::Values(8000, 16000, 32000, 48000));

}  // namespace
}  // namespace webrtc