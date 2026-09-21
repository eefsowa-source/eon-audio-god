cmake_minimum_required(VERSION 3.22)
project(EON_GOD VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EON_BUILD_TESTS "Build the standalone DSP smoke test" ON)
option(EON_BUILD_PLUGINS "Build JUCE plugins and JUCE-dependent measurement tools" ON)
option(EON_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Pure-DSP core, vendored at eon_dsp/. Developers of sibling EON projects
# can point this at the shared canonical copy: -DEON_DSP_DIR=../eon_dsp
set(EON_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp"
    CACHE PATH "Path to the eon_dsp header library")

set(EON_DSP_REQUIRED_HEADERS
    Adaa.h
    Measure.h
    Oversampling.h
    Rng.h
    RtGuard.h
    Solvers.h
    Stages.h
    Transformer.h
    Triode.h
    Wdf.h
    Zdf.h
)

foreach(header IN LISTS EON_DSP_REQUIRED_HEADERS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp/Dsp/${header}")
        message(FATAL_ERROR "Required vendored DSP snapshot header is missing: eon_dsp/Dsp/${header}")
    endif()
    if(NOT EXISTS "${EON_DSP_DIR}/Dsp/${header}")
        message(FATAL_ERROR "Required DSP header is missing from EON_DSP_DIR: ${EON_DSP_DIR}/Dsp/${header}")
    endif()
endforeach()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp/REVISION")
    message(FATAL_ERROR "Required vendored DSP snapshot revision is missing: eon_dsp/REVISION")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp/REVISION" EON_DSP_SNAPSHOT_REVISION)
string(STRIP "${EON_DSP_SNAPSHOT_REVISION}" EON_DSP_SNAPSHOT_REVISION)
string(LENGTH "${EON_DSP_SNAPSHOT_REVISION}" EON_DSP_SNAPSHOT_REVISION_LENGTH)
if(NOT EON_DSP_SNAPSHOT_REVISION_LENGTH EQUAL 40 OR
   NOT EON_DSP_SNAPSHOT_REVISION MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "eon_dsp/REVISION must contain a 40-character Git commit hash")
endif()

add_library(eon_dsp_headers INTERFACE)
add_library(eon::dsp ALIAS eon_dsp_headers)
target_include_directories(eon_dsp_headers INTERFACE "${EON_DSP_DIR}")
target_compile_features(eon_dsp_headers INTERFACE cxx_std_20)

if(EON_BUILD_PLUGINS)
    add_subdirectory(JUCE)

    function(add_eon_god name product_name code)
        juce_add_plugin(${name}
            COMPANY_NAME "EON AUDIO"
            COMPANY_WEBSITE "https://eon.audio"
            COMPANY_EMAIL "dev@eon.audio"
            BUNDLE_ID "audio.eon.${name}"
            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT FALSE
            NEEDS_MIDI_OUTPUT FALSE
            IS_MIDI_EFFECT FALSE
            COPY_PLUGIN_AFTER_BUILD TRUE
            PLUGIN_MANUFACTURER_CODE EONA
            PLUGIN_CODE ${code}
            FORMATS VST3 AU Standalone
            PRODUCT_NAME ${product_name}
            VST3_CATEGORIES "EQ"
        )
        target_sources(${name} PRIVATE
            Source/${name}/PluginProcessor.cpp
            Source/${name}/PluginEditor.cpp
        )
        target_include_directories(${name} PRIVATE Source/Shared Source/${name})
        target_link_libraries(${name} PRIVATE
            eon::dsp
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_audio_processors
            juce::juce_graphics
        )
        target_compile_definitions(${name} PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DSP_USE_SIMD=1
            EON_GOD_MODE=1
        )
        # fast math everywhere; arch-specific flags only where they exist
        if(MSVC)
            target_compile_options(${name} PRIVATE /arch:AVX2 /O2 /fp:fast)
        else()
            target_compile_options(${name} PRIVATE -O3 -ffast-math -funroll-loops)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
                target_compile_options(${name} PRIVATE -mavx2 -mfma)
            endif()
        endif()
        if(EON_ENABLE_SANITIZERS)
            if(MSVC)
                target_compile_options(${name} PRIVATE /fsanitize=address)
                target_link_options(${name} PRIVATE /fsanitize=address)
            else()
                target_compile_options(${name} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(${name} PRIVATE -fsanitize=address,undefined)
            endif()
        endif()
        juce_generate_juce_header(${name})
    endfunction()

    add_eon_god(Neve1073 "EON 1073 GOD" "E73G")
    add_eon_god(Avalon737 "EON 737 GOD" "E37G")
endif()

if(EON_BUILD_TESTS)
    enable_testing()
    add_executable(dsp_smoke_test Docs/dsp_smoke_test.cpp)
    target_link_libraries(dsp_smoke_test PRIVATE eon::dsp)
    if(NOT MSVC)
        target_compile_options(dsp_smoke_test PRIVATE -O2 -Wall -Wextra)
    endif()
    if(EON_ENABLE_SANITIZERS)
        if(MSVC)
            target_compile_options(dsp_smoke_test PRIVATE /fsanitize=address)
            target_link_options(dsp_smoke_test PRIVATE /fsanitize=address)
        else()
            target_compile_options(dsp_smoke_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(dsp_smoke_test PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    add_test(NAME dsp_smoke COMMAND dsp_smoke_test)

    if(EON_BUILD_PLUGINS)
        function(add_processor_tests test_target processor kind)
            get_target_property(processor_juce_code ${processor} JUCE_GENERATED_SOURCES_DIRECTORY)
            if(NOT processor_juce_code)
                message(FATAL_ERROR "${processor} does not expose a generated JUCE source directory")
            endif()

            add_executable(${test_target} Tests/ProcessorTest.cpp)
            target_include_directories(${test_target} PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/Source"
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/Shared"
                "${CMAKE_CURRENT_SOURCE_DIR}/Source/${processor}"
                "${processor_juce_code}"
            )
            target_link_libraries(${test_target} PRIVATE
                ${processor}
                eon::dsp
                juce::juce_audio_utils
                juce::juce_dsp
                juce::juce_audio_processors
            )
            target_compile_definitions(${test_target} PRIVATE
                EON_PROCESSOR_KIND=${kind}
                EON_PROCESSOR_TESTING=1
                JUCE_WEB_BROWSER=0
                JUCE_USE_CURL=0
            )
            if(EON_ENABLE_SANITIZERS)
                if(MSVC)
                    target_compile_options(${test_target} PRIVATE /fsanitize=address)
                    target_link_options(${test_target} PRIVATE /fsanitize=address)
                else()
                    target_compile_options(${test_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
                    target_link_options(${test_target} PRIVATE -fsanitize=address,undefined)
                endif()
            endif()
            add_test(NAME ${test_target} COMMAND ${test_target})
        endfunction()

        add_processor_tests(neve_processor_tests Neve1073 1073)
        add_processor_tests(avalon_processor_tests Avalon737 737)

        # Offline measurement dumper (EQ curves + THD -> CSV). Not a ctest —
        # run manually: ./build/eq_response_dump measurements
        add_executable(eq_response_dump Docs/eq_response_dump.cpp)
        target_link_libraries(eq_response_dump PRIVATE eon::dsp juce::juce_dsp)
        if(NOT MSVC)
            target_compile_options(eq_response_dump PRIVATE -O2)
        endif()

        # Alias-foldback measurement (JUCE vs eon oversampler vs 1x). Not a
        # ctest — run manually: ./build/alias_dump measurements
        add_executable(alias_dump Docs/alias_dump.cpp)
        target_link_libraries(alias_dump PRIVATE eon::dsp juce::juce_dsp)
        if(NOT MSVC)
            target_compile_options(alias_dump PRIVATE -O2)
        endif()
    endif()
endif()
