# samples/net — networking-stack samples (headless).
#
# phase911-sample-consolidation: per
# [[feedback-sample-consolidation-and-per-feature-demo]], all 5 headless
# net "console proof" smokes have been folded into the source libraries'
# gtests. Source directories deleted in this commit.
#
# Retired in phase 911:
#   * hello_udp             -> engine/world/net/tests/test_net.cpp (UdpConnection
#       BuildPairAndRoundTrip + ReceiveWithoutDataReturnsWouldBlock, plus
#       ChannelMux + reliable channel cases in test_net_reliable.cpp).
#   * hello_net_sim         -> engine/world/net/tests/test_net.cpp (69)
#       + test_net_qos.cpp (7) + test_net_reconciler.cpp (7) + test_net_reliable.cpp (7)
#   * hello_netcode         -> engine/world/net/tests/test_net_reconciler.cpp (7)
#       + test_net_reliable.cpp (7) + test_net_qos.cpp (7)
#   * hello_matchmaker      -> engine/world/net_matchmaker/tests/test_matchmaker.cpp (11)
#   * hello_session_replay  -> engine/world/net_session_replay/tests/test_session_replay.cpp (7)
#
# samples/net/ now empty.
