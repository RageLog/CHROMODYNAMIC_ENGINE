cd_add_test(net_lobby
  SOURCES test_lobby.cpp
  DEPS    cd::net_lobby cd::core
)

cd_add_test(net_lobby_transport
  SOURCES test_packet_transport.cpp
  DEPS    cd::net_lobby cd::core
)
