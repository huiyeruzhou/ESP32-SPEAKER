idf_component_register(SRCS "hello_world_main.cpp"
	INCLUDE_DIRS ""
	REQUIRES lwip udp_broadcast wifi_config ability_framework esp_http_server)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
