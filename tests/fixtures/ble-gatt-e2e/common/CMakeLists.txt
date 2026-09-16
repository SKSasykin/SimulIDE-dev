idf_component_register(SRCS "virtual_vhci.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_vhci_host_register_callback"
    "-Wl,--wrap=esp_vhci_host_check_send_available"
    "-Wl,--wrap=esp_vhci_host_send_packet")