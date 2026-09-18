cmake_minimum_required(VERSION 3.22)
project(EON_GOD VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(EON_BUILD_TESTS "Build the standalone DSP smoke test" ON)
option(EON_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Pure-DSP core, vendored at eon_dsp/. Developers of sibling EON projects
# can point this at the shared canonical copy: -DEON_DSP_DIR=../eon_dsp
set(EON_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp"
    CACHE PATH "Path to the eon_dsp header library")

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
    target_include_directories(${name} PRIVATE Source/Shared Source/${name} ${EON_DSP_DIR})
    target_link_libraries(${name} PRIVATE
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

if(EON_BUILD_TESTS)
    enable_testing()
    add_executable(dsp_smoke_test Docs/dsp_smoke_test.cpp)
    target_include_directories(dsp_smoke_test PRIVATE ${EON_DSP_DIR})
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

    # Offline measurement dumper (EQ curves + THD -> CSV). Not a ctest —
    # run manually: ./build/eq_response_dump measurements
    add_executable(eq_response_dump Docs/eq_response_dump.cpp)
    target_include_directories(eq_response_dump PRIVATE ${EON_DSP_DIR})
    target_link_libraries(eq_response_dump PRIVATE juce::juce_dsp)
    if(NOT MSVC)
        target_compile_options(eq_response_dump PRIVATE -O2)
    endif()
endif()
