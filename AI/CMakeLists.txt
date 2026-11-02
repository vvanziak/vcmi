#######################################
#        Add subdirectories           #
#######################################

# Combat AI's
if(ENABLE_STUPID_AI)
	add_subdirectory(StupidAI)
endif()

if(ENABLE_BATTLE_AI)
	add_subdirectory(BattleAI)
endif()

if(ENABLE_MMAI)
	add_subdirectory(MMAI)
endif()

# Adventure AI's
add_subdirectory(EmptyAI)

if(ENABLE_NULLKILLER2_AI)
	add_subdirectory(Nullkiller2)
endif()

# Adventure AI's built from directories outside of this tree: -DVCMI_EXTRA_AI_DIRS="/path/a;/path/b".
# Each directory has a CMakeLists.txt that defines its target and appends the target name to the global property VCMI_EXTRA_AI_TARGETS.
foreach(extraAiDir ${VCMI_EXTRA_AI_DIRS})
	get_filename_component(extraAiName ${extraAiDir} NAME)
	add_subdirectory(${extraAiDir} ${CMAKE_BINARY_DIR}/AI/extra/${extraAiName})
endforeach()
