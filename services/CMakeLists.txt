# services/ — orchestration layer. Services know multiple drivers
# and implement attack-level features. Built only in firmware mode.

add_subdirectory(glitch_engine)
add_subdirectory(host_proto)
add_subdirectory(swd_core)
add_subdirectory(jtag_core)
add_subdirectory(pinout_scanner)
add_subdirectory(buspirate_compat)
add_subdirectory(flashrom_serprog)
add_subdirectory(swd_bus_lock)
add_subdirectory(campaign_manager)
