add_executable(test_stats_overlay_config
  stats_overlay/test_stats_overlay_config.c
  ${CMAKE_SOURCE_DIR}/src/stats_overlay.c
)
target_include_directories(test_stats_overlay_config PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(test_stats_overlay_config PRIVATE ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/src)
target_link_libraries(test_stats_overlay_config m moonlight-common ${OPENSSL_CRYPTO_LIBRARY})
add_test(NAME test_stats_overlay_config COMMAND test_stats_overlay_config)

add_executable(test_stats_overlay_runtime
  stats_overlay/test_stats_overlay_runtime.c
  ${CMAKE_SOURCE_DIR}/src/stats_overlay.c
  ${CMAKE_SOURCE_DIR}/src/stats_overlay_draw.c
)
target_include_directories(test_stats_overlay_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(test_stats_overlay_runtime PRIVATE ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/src)
target_link_libraries(test_stats_overlay_runtime m moonlight-common ${OPENSSL_CRYPTO_LIBRARY})
add_test(NAME test_stats_overlay_runtime COMMAND test_stats_overlay_runtime)
