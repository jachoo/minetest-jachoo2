project(minetest)
cmake_minimum_required( VERSION 2.6 )

if(RUN_IN_PLACE)
	add_definitions ( -DRUN_IN_PLACE )
endif(RUN_IN_PLACE)

# user-visible option to enable/disable gettext usage
OPTION(ENABLE_GETTEXT "Use GetText for internationalization" ON)

# this is only set to 1 if gettext is enabled _and_ available
set(USE_GETTEXT 0)

find_package(GettextLib)

if(GETTEXT_FOUND AND ENABLE_GETTEXT)
	message(STATUS "gettext include path: ${GETTEXT_INCLUDE_DIR}")
	message(STATUS "gettext msgfmt path: ${GETTEXT_MSGFMT}")
	if(WIN32)
		message(STATUS "gettext library: ${GETTEXT_LIBRARY}")
		message(STATUS "gettext dll: ${GETTEXT_DLL}")
		message(STATUS "gettext iconv dll: ${GETTEXT_ICONV_DLL}")
	endif()
	set(USE_GETTEXT 1)
	message(STATUS "GetText enabled; locales found: ${GETTEXT_AVAILABLE_LOCALES}")
elseif(GETTEXT_FOUND AND NOT ENABLE_GETTEXT)
	MESSAGE(STATUS "GetText found but disabled;")
else(GETTEXT_FOUND AND ENABLE_GETTEXT)
	message(STATUS "GetText disabled")
endif(GETTEXT_FOUND AND ENABLE_GETTEXT)

if(NOT MSVC)
	set(USE_GPROF 0 CACHE BOOL "Use -pg flag for g++")
endif()

# Use cmake_config.h
add_definitions ( -DUSE_CMAKE_CONFIG_H )

if(WIN32)
	# Windows
	if(MSVC) # MSVC Specifics
		# Surpress some useless warnings
		add_definitions ( /D "_CRT_SECURE_NO_DEPRECATE" /W1 )
	else() # Probably MinGW = GCC
		set(PLATFORM_LIBS ws2_32.lib)
	endif()
	# Zlib stuff
	set(ZLIB_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/../../zlib/zlib-1.2.5"
			CACHE PATH "Zlib include directory")
	set(ZLIB_LIBRARIES "${PROJECT_SOURCE_DIR}/../../zlib125dll/dll32/zlibwapi.lib"
			CACHE FILEPATH "Path to zlibwapi.lib")
	set(ZLIB_DLL "${PROJECT_SOURCE_DIR}/../../zlib125dll/dll32/zlibwapi.dll"
			CACHE FILEPATH "Path to zlibwapi.dll (for installation)")
	set(IRRLICHT_SOURCE_DIR "${PROJECT_SOURCE_DIR}/../../irrlicht-1.7.2"
			CACHE PATH "irrlicht dir")
else()
	# Unix probably
	if(BUILD_CLIENT)
		find_package(X11 REQUIRED)
		find_package(OpenGL REQUIRED)
		find_package(JPEG REQUIRED)
		find_package(BZip2 REQUIRED)
		find_package(PNG REQUIRED)
		if(APPLE)
			FIND_LIBRARY(CARBON_LIB Carbon)
			FIND_LIBRARY(COCOA_LIB Cocoa)
			FIND_LIBRARY(IOKIT_LIB IOKit)
			mark_as_advanced(
				CARBON_LIB
				COCOA_LIB
				IOKIT_LIB
			)
			SET(CLIENT_PLATFORM_LIBS ${CLIENT_PLATFORM_LIBS} ${CARBON_LIB} ${COCOA_LIB} ${IOKIT_LIB})
		endif(APPLE)
	endif(BUILD_CLIENT)
	find_package(ZLIB REQUIRED)
	set(PLATFORM_LIBS -lpthread ${CMAKE_DL_LIBS})
	#set(CLIENT_PLATFORM_LIBS -lXxf86vm)
	# This way Xxf86vm is found on OpenBSD too
	find_library(XXF86VM_LIBRARY Xxf86vm)
	set(CLIENT_PLATFORM_LIBS ${CLIENT_PLATFORM_LIBS} ${XXF86VM_LIBRARY})
endif()

find_package(Jthread REQUIRED)
find_package(Sqlite3 REQUIRED)

# TODO: Create proper find script for Lua
set(LUA_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lua/src")
set(LUA_LIBRARY "lua")

configure_file(
	"${PROJECT_SOURCE_DIR}/cmake_config.h.in"
	"${PROJECT_BINARY_DIR}/cmake_config.h"
)

set(common_SRCS
	inventorymanager.cpp
	mods.cpp
	serverremoteplayer.cpp
	content_abm.cpp
	craftdef.cpp
	nameidmapping.cpp
	itemdef.cpp
	nodedef.cpp
	luaentity_common.cpp
	scriptapi.cpp
	script.cpp
	log.cpp
	content_sao.cpp
	mapgen.cpp
	content_nodemeta.cpp
	content_mapnode.cpp
	auth.cpp
	collision.cpp
	nodemetadata.cpp
	serverobject.cpp
	noise.cpp
	porting.cpp
	materials.cpp
	defaultsettings.cpp
	mapnode.cpp
	voxel.cpp
	inventory.cpp
	debug.cpp
	serialization.cpp
	light.cpp
	filesys.cpp
	connection.cpp
	environment.cpp
	server.cpp
	servercommand.cpp
	socket.cpp
	mapblock.cpp
	mapsector.cpp
	map.cpp
	player.cpp
	utility.cpp
	test.cpp
	sha1.cpp
	base64.cpp
	ban.cpp
	db.cpp
)

# This gives us the icon
if(WIN32)
	if(MINGW)
		ADD_CUSTOM_COMMAND(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/winresource_rc.o
			COMMAND windres.exe -I${CMAKE_CURRENT_SOURCE_DIR}
			-i${CMAKE_CURRENT_SOURCE_DIR}/winresource.rc
			-o ${CMAKE_CURRENT_BINARY_DIR}/winresource_rc.o)
		SET(common_SRCS ${common_SRCS} ${CMAKE_CURRENT_BINARY_DIR}/winresource_rc.o)
	else(MINGW) # Probably MSVC
		set(common_SRCS ${common_SRCS} winresource.rc)
	endif(MINGW)
endif()

# Client sources
set(minetest_SRCS
	${common_SRCS}
	content_mapblock.cpp
	content_cao.cpp
	mesh.cpp
	mapblock_mesh.cpp
	farmesh.cpp
	keycode.cpp
	camera.cpp
	clouds.cpp
	clientobject.cpp
	guiMainMenu.cpp
	guiKeyChangeMenu.cpp
	guiMessageMenu.cpp
	guiTextInputMenu.cpp
	guiInventoryMenu.cpp
	guiPauseMenu.cpp
	guiPasswordChange.cpp
	guiDeathScreen.cpp
	client.cpp
	tile.cpp
	game.cpp
	main.cpp
)

# Server sources
set(minetestserver_SRCS
	${common_SRCS}
	servermain.cpp
)

include_directories(
	${PROJECT_BINARY_DIR}
	${IRRLICHT_INCLUDE_DIR}
	${ZLIB_INCLUDE_DIR}
	${CMAKE_BUILD_TYPE}
	${PNG_INCLUDE_DIR}
	${GETTEXT_INCLUDE_DIR}
	${JTHREAD_INCLUDE_DIR}
	${SQLITE3_INCLUDE_DIR}
	${LUA_INCLUDE_DIR}
)

set(EXECUTABLE_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin")

if(BUILD_CLIENT)
	add_executable(${PROJECT_NAME} ${minetest_SRCS})
	target_link_libraries(
		${PROJECT_NAME}
		${ZLIB_LIBRARIES}
		${IRRLICHT_LIBRARY}
		${OPENGL_LIBRARIES}
		${JPEG_LIBRARIES}
		${BZIP2_LIBRARIES}
		${PNG_LIBRARIES}
		${X11_LIBRARIES}
		${GETTEXT_LIBRARY}
		${JTHREAD_LIBRARY}
		${SQLITE3_LIBRARY}
		${LUA_LIBRARY}
		${PLATFORM_LIBS}
		${CLIENT_PLATFORM_LIBS}
	)
endif(BUILD_CLIENT)

if(BUILD_SERVER)
	add_executable(${PROJECT_NAME}server ${minetestserver_SRCS})
	target_link_libraries(
		${PROJECT_NAME}server
		${ZLIB_LIBRARIES}
		${JTHREAD_LIBRARY}
		${SQLITE3_LIBRARY}
		${LUA_LIBRARY}
		${PLATFORM_LIBS}
	)
endif(BUILD_SERVER)

#
# Set some optimizations and tweaks
#

include(CheckCXXCompilerFlag)

if(MSVC)
	# Visual Studio

	# EHa enables SEH exceptions (used for catching segfaults)
	set(CMAKE_CXX_FLAGS_RELEASE "/EHa /O2 /Ob2 /Oi /Ot /Oy /GL /FD /MT /GS- /arch:SSE /fp:fast /D NDEBUG /D _HAS_ITERATOR_DEBUGGING=0 /TP")
	#set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /NODEFAULTLIB:\"libcmtd.lib\" /NODEFAULTLIB:\"libcmt.lib\"")
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")

	# Debug build doesn't catch exceptions by itself
	# Add some optimizations because otherwise it's VERY slow
	set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /RTC1")

	# Flags for C files (sqlite)
	# /MT = Link statically with standard library stuff
	set(CMAKE_C_FLAGS_RELEASE "/O2 /Ob2 /MT")
	
	if(BUILD_SERVER)
		set_target_properties(${PROJECT_NAME}server PROPERTIES
				COMPILE_DEFINITIONS "SERVER")
	endif(BUILD_SERVER)

else()
	# Probably GCC
	
	if(WARN_ALL)
		set(RELEASE_WARNING_FLAGS "-Wall")
	else()
		set(RELEASE_WARNING_FLAGS "")
	endif()
	
	if(NOT APPLE AND NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
		CHECK_CXX_COMPILER_FLAG("-Wno-unused-but-set-variable" HAS_UNUSED_BUT_SET_VARIABLE_WARNING)
		if(HAS_UNUSED_BUT_SET_VARIABLE_WARNING)
			set(WARNING_FLAGS "${WARNING_FLAGS} -Wno-unused-but-set-variable")
		endif(HAS_UNUSED_BUT_SET_VARIABLE_WARNING)
	endif()
	
	if(APPLE)
		set(CMAKE_OSX_ARCHITECTURES i386 CACHE STRING "do not build for 64-bit" FORCE)
		set(ARCH i386)
	endif()

	set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG ${RELEASE_WARNING_FLAGS} ${WARNING_FLAGS} -O3 -ffast-math -Wall -fomit-frame-pointer -pipe -funroll-loops")
	set(CMAKE_CXX_FLAGS_DEBUG "-g -O1 -Wall ${WARNING_FLAGS}")

	if(USE_GPROF)
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pg")
	endif()
	
	if(BUILD_SERVER)
		set_target_properties(${PROJECT_NAME}server PROPERTIES
				COMPILE_DEFINITIONS "SERVER")
	endif(BUILD_SERVER)

endif()

#MESSAGE(STATUS "CMAKE_CXX_FLAGS_RELEASE=${CMAKE_CXX_FLAGS_RELEASE}")
#MESSAGE(STATUS "CMAKE_CXX_FLAGS_DEBUG=${CMAKE_CXX_FLAGS_DEBUG}")

#
# Installation
#

# Example configuration file
# NOTE: Install is handled elsewhere
#install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../minetest.conf.example" DESTINATION ${EXAMPLE_CONF_DIR})

if(BUILD_CLIENT)
	install(TARGETS ${PROJECT_NAME} DESTINATION ${BINDIR})

	#file(GLOB images "${CMAKE_CURRENT_SOURCE_DIR}/../data/textures/*.png")
	#install(FILES ${images} DESTINATION ${DATADIR}/textures)
	#file(GLOB datasubstuff "${CMAKE_CURRENT_SOURCE_DIR}/../data/*")
	#install(DIRECTORY "${datasubstuff}" DESTINATION ${DATADIR})
	install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../data/" DESTINATION ${DATADIR})

	if(USE_GETTEXT)
		foreach(LOCALE ${GETTEXT_AVAILABLE_LOCALES})
			set_mo_paths(MO_BUILD_PATH MO_DEST_PATH ${LOCALE})
			set(MO_BUILD_PATH "${MO_BUILD_PATH}/${PROJECT_NAME}.mo")
			install(FILES ${MO_BUILD_PATH} DESTINATION ${MO_DEST_PATH})
		endforeach(LOCALE ${GETTEXT_AVAILABLE_LOCALES})
	endif()

	if(WIN32)
		if(DEFINED IRRLICHT_DLL)
			install(FILES ${IRRLICHT_DLL} DESTINATION ${BINDIR})
		endif()
		if(DEFINED ZLIB_DLL)
			install(FILES ${ZLIB_DLL} DESTINATION ${BINDIR})
		endif()
		if(USE_GETTEXT)
			if(DEFINED GETTEXT_DLL)
				install(FILES ${GETTEXT_DLL} DESTINATION ${BINDIR})
			endif()
			if(DEFINED GETTEXT_ICONV_DLL)
				install(FILES ${GETTEXT_ICONV_DLL} DESTINATION ${BINDIR})
			endif()
		endif(USE_GETTEXT)
	endif()
endif(BUILD_CLIENT)

if(BUILD_SERVER)
	install(TARGETS ${PROJECT_NAME}server DESTINATION ${BINDIR})
endif(BUILD_SERVER)

if (USE_GETTEXT)
	set(MO_FILES)

	foreach(LOCALE ${GETTEXT_AVAILABLE_LOCALES})
		set(PO_FILE_PATH "${GETTEXT_PO_PATH}/${LOCALE}/minetest.po")
		set_mo_paths(MO_BUILD_PATH MO_DEST_PATH ${LOCALE})
		set(MO_FILE_PATH "${MO_BUILD_PATH}/${PROJECT_NAME}.mo")

		add_custom_command(OUTPUT ${MO_BUILD_PATH}
			COMMAND ${CMAKE_COMMAND} -E make_directory ${MO_BUILD_PATH}
			COMMENT "mo-update [${LOCALE}]: Creating locale directory.")

		add_custom_command(
			OUTPUT ${MO_FILE_PATH}
			COMMAND ${GETTEXT_MSGFMT} -o ${MO_FILE_PATH} ${PO_FILE_PATH}
			DEPENDS ${MO_BUILD_PATH} ${PO_FILE_PATH}
			WORKING_DIRECTORY "${GETTEXT_PO_PATH}/${LOCALE}"
			COMMENT "mo-update [${LOCALE}]: Creating mo file."
			)

		set(MO_FILES ${MO_FILES} ${MO_FILE_PATH})
	endforeach(LOCALE ${GETTEXT_AVAILABLE_LOCALES})

	add_custom_target(translations ALL COMMENT "mo update" DEPENDS ${MO_FILES})
endif(USE_GETTEXT)

# Subdirectories

if (JTHREAD_FOUND)
else (JTHREAD_FOUND)
	add_subdirectory(jthread)
endif (JTHREAD_FOUND)

if (SQLITE3_FOUND)
else (SQLITE3_FOUND)
	add_subdirectory(sqlite)
endif (SQLITE3_FOUND)

if (LUA_FOUND)
else (LUA_FOUND)
	add_subdirectory(lua)
endif (LUA_FOUND)

#end
