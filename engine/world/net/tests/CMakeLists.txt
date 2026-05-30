cd_add_test(net
  SOURCES test_net.cpp
  DEPS    cd::net cd::core
)

cd_add_test(net_reliable
  SOURCES test_net_reliable.cpp
  DEPS    cd::net cd::core
)

cd_add_test(net_qos
  SOURCES test_net_qos.cpp
  DEPS    cd::net cd::core
)

cd_add_test(net_reconciler
  SOURCES test_net_reconciler.cpp
  DEPS    cd::net cd::core
)
