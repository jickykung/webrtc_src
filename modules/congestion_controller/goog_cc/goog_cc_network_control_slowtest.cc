/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/transport/goog_cc_factory.h"
#include "test/field_trial.h"
#include "test/gtest.h"
#include "test/scenario/scenario.h"

namespace webrtc {
namespace test {

TEST(GoogCcNetworkControllerTest, MaintainsLowRateInSafeResetTrial) {
  const DataRate kLinkCapacity = DataRate::kbps(200);
  const DataRate kStartRate = DataRate::kbps(300);

  ScopedFieldTrials trial("WebRTC-Bwe-SafeResetOnRouteChange/Enabled/");
  Scenario s("googcc_unit/safe_reset_low", true);
  auto* send_net = s.CreateSimulationNode([&](NetworkNodeConfig* c) {
    c->simulation.bandwidth = kLinkCapacity;
    c->simulation.delay = TimeDelta::ms(10);
  });
  // TODO(srte): replace with SimulatedTimeClient when it supports probing.
  auto* client = s.CreateClient("send", [&](CallClientConfig* c) {
    c->transport.cc = TransportControllerConfig::CongestionController::kGoogCc;
    c->transport.rates.start_rate = kStartRate;
  });
  auto* route = s.CreateRoutes(client, {send_net},
                               s.CreateClient("return", CallClientConfig()),
                               {s.CreateSimulationNode(NetworkNodeConfig())});
  s.CreateVideoStream(route->forward(), VideoStreamConfig());
  // Allow the controller to stabilize.
  s.RunFor(TimeDelta::ms(500));
  EXPECT_NEAR(client->send_bandwidth().kbps(), kLinkCapacity.kbps(), 50);
  s.ChangeRoute(route->forward(), {send_net});
  // Allow new settings to propagate.
  s.RunFor(TimeDelta::ms(100));
  // Under the trial, the target should be unchanged for low rates.
  EXPECT_NEAR(client->send_bandwidth().kbps(), kLinkCapacity.kbps(), 50);
}

TEST(GoogCcNetworkControllerTest, CutsHighRateInSafeResetTrial) {
  const DataRate kLinkCapacity = DataRate::kbps(1000);
  const DataRate kStartRate = DataRate::kbps(300);

  ScopedFieldTrials trial("WebRTC-Bwe-SafeResetOnRouteChange/Enabled/");
  Scenario s("googcc_unit/safe_reset_high_cut", true);
  auto send_net = s.CreateSimulationNode([&](NetworkNodeConfig* c) {
    c->simulation.bandwidth = kLinkCapacity;
    c->simulation.delay = TimeDelta::ms(50);
  });
  // TODO(srte): replace with SimulatedTimeClient when it supports probing.
  auto* client = s.CreateClient("send", [&](CallClientConfig* c) {
    c->transport.cc = TransportControllerConfig::CongestionController::kGoogCc;
    c->transport.rates.start_rate = kStartRate;
  });
  auto* route = s.CreateRoutes(client, {send_net},
                               s.CreateClient("return", CallClientConfig()),
                               {s.CreateSimulationNode(NetworkNodeConfig())});
  s.CreateVideoStream(route->forward(), VideoStreamConfig());
  // Allow the controller to stabilize.
  s.RunFor(TimeDelta::ms(500));
  EXPECT_NEAR(client->send_bandwidth().kbps(), kLinkCapacity.kbps(), 300);
  s.ChangeRoute(route->forward(), {send_net});
  // Allow new settings to propagate.
  s.RunFor(TimeDelta::ms(50));
  // Under the trial, the target should be reset from high values.
  EXPECT_NEAR(client->send_bandwidth().kbps(), kStartRate.kbps(), 30);
}

TEST(GoogCcNetworkControllerTest, DetectsHighRateInSafeResetTrial) {
  ScopedFieldTrials trial(
      "WebRTC-Bwe-SafeResetOnRouteChange/Enabled,ack/"
      "WebRTC-Bwe-ProbeRateFallback/Enabled/");
  const DataRate kInitialLinkCapacity = DataRate::kbps(200);
  const DataRate kNewLinkCapacity = DataRate::kbps(800);
  const DataRate kStartRate = DataRate::kbps(300);

  Scenario s("googcc_unit/safe_reset_high_detect", true);
  auto* initial_net = s.CreateSimulationNode([&](NetworkNodeConfig* c) {
    c->simulation.bandwidth = kInitialLinkCapacity;
    c->simulation.delay = TimeDelta::ms(50);
  });
  auto* new_net = s.CreateSimulationNode([&](NetworkNodeConfig* c) {
    c->simulation.bandwidth = kNewLinkCapacity;
    c->simulation.delay = TimeDelta::ms(50);
  });
  // TODO(srte): replace with SimulatedTimeClient when it supports probing.
  auto* client = s.CreateClient("send", [&](CallClientConfig* c) {
    c->transport.cc = TransportControllerConfig::CongestionController::kGoogCc;
    c->transport.rates.start_rate = kStartRate;
  });
  auto* route = s.CreateRoutes(client, {initial_net},
                               s.CreateClient("return", CallClientConfig()),
                               {s.CreateSimulationNode(NetworkNodeConfig())});
  s.CreateVideoStream(route->forward(), VideoStreamConfig());
  // Allow the controller to stabilize.
  s.RunFor(TimeDelta::ms(1000));
  EXPECT_NEAR(client->send_bandwidth().kbps(), kInitialLinkCapacity.kbps(), 50);
  s.ChangeRoute(route->forward(), {new_net});
  // Allow new settings to propagate, but not probes to be received.
  s.RunFor(TimeDelta::ms(50));
  // Under the field trial, the target rate should be unchanged since it's lower
  // than the starting rate.
  EXPECT_NEAR(client->send_bandwidth().kbps(), kInitialLinkCapacity.kbps(), 50);
  // However, probing should have made us detect the higher rate.
  s.RunFor(TimeDelta::ms(2000));
  EXPECT_GT(client->send_bandwidth().kbps(), kNewLinkCapacity.kbps() - 300);
}

TEST(GoogCcNetworkControllerTest,
     TargetRateReducedOnPacingBufferBuildupInTrial) {
  // Configure strict pacing to ensure build-up.
  ScopedFieldTrials trial(
      "WebRTC-CongestionWindowPushback/Enabled/WebRTC-CwndExperiment/"
      "Enabled-100/WebRTC-Video-Pacing/factor:1.0/"
      "WebRTC-AddPacingToCongestionWindowPushback/Enabled/");

  const DataRate kLinkCapacity = DataRate::kbps(1000);
  const DataRate kStartRate = DataRate::kbps(1000);

  Scenario s("googcc_unit/pacing_buffer_buildup", true);
  auto* net = s.CreateSimulationNode([&](NetworkNodeConfig* c) {
    c->simulation.bandwidth = kLinkCapacity;
    c->simulation.delay = TimeDelta::ms(50);
  });
  // TODO(srte): replace with SimulatedTimeClient when it supports pacing.
  auto* client = s.CreateClient("send", [&](CallClientConfig* c) {
    c->transport.cc = TransportControllerConfig::CongestionController::kGoogCc;
    c->transport.rates.start_rate = kStartRate;
  });
  auto* route = s.CreateRoutes(client, {net},
                               s.CreateClient("return", CallClientConfig()),
                               {s.CreateSimulationNode(NetworkNodeConfig())});
  s.CreateVideoStream(route->forward(), VideoStreamConfig());
  // Allow some time for the buffer to build up.
  s.RunFor(TimeDelta::seconds(5));

  // Without trial, pacer delay reaches ~250 ms.
  EXPECT_LT(client->GetStats().pacer_delay_ms, 150);
}

}  // namespace test
}  // namespace webrtc
