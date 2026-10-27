set(claudeBridge_SRCS
		BridgeServer.cpp
		ClaudeBridgeAI.cpp
)

set(claudeBridge_HEADERS
		StdInc.h

		BridgeServer.h
		ClaudeBridgeAI.h
)

assign_source_group(${claudeBridge_SRCS} ${claudeBridge_HEADERS})

add_library(ClaudeBridge OBJECT ${claudeBridge_SRCS} ${claudeBridge_HEADERS})
target_compile_definitions(ClaudeBridge PRIVATE VCMI_DLL=1)

target_include_directories(ClaudeBridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ClaudeBridge PRIVATE vcmiMain)

vcmi_set_output_dir(ClaudeBridge "AI")
enable_pch(ClaudeBridge)
