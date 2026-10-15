cmake_minimum_required(VERSION 3.22)
project(EON_GOD VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EON_BUILD_PLUGINS "Build the VST3/AU/Standalone plugin targets" ON)
option(EON_COPY_PLUGINS "Copy built plugins into the user's plugin folders" ON)
option(EON_BUILD_TESTS "Build the DSP smoke test and the quality harness" ON)
option(EON_ENABLE_SANITIZERS "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(EON_FAST_MATH "Compile the DSP with -ffast-math (not recommended, see below)" OFF)

# ---------------------------------------------------------------------------
# Floating-point policy.
#
# The emulation solvers (WDF diode brackets, Lambert W, triode Newton, ADAA
# divided differences) guard themselves with std::isfinite() and NaN tests.
# -ffast-math assumes no NaN/Inf, permits reassociation, and lets the compiler
# delete those checks, so it is off by default and only available as an
# explicit opt-in for A/B measurement. The release default keeps the useful
# subset: no errno setting, no FP exceptions, and FMA contraction enabled.
# ---------------------------------------------------------------------------
set(EON_FP_FLAGS "")
if(NOT MSVC)
    if(EON_FAST_MATH)
        list(APPEND EON_FP_FLAGS -ffast-math)
    else()
        list(APPEND EON_FP_FLAGS -fno-math-errno -fno-trapping-math -ffp-contract=fast)
    endif()
endif()

# Correctness tests run with contraction disabled. The product build contracts
# (FMA), which is faster but makes two identical DSP objects diverge in the last
# ulp depending on how each call site was scheduled — that is fine for audio and
# fatal for a bit-identity assertion. Set EON_TEST_FAST_MATH to run the core
# tests under the shipping fast-math model instead (parity checking only).
option(EON_TEST_FAST_MATH "Compile the core tests with -ffast-math too" OFF)

set(EON_TEST_FP_FLAGS "")
if(NOT MSVC)
    if(EON_TEST_FAST_MATH)
        list(APPEND EON_TEST_FP_FLAGS -ffast-math)
    else()
        list(APPEND EON_TEST_FP_FLAGS -fno-math-errno -fno-trapping-math -ffp-contract=off)
    endif()
endif()

# Pure-DSP core, vendored at eon_dsp/. Developers of sibling EON projects
# can point this at the shared canonical copy: -DEON_DSP_DIR=../eon_dsp
set(EON_DSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/eon_dsp"
    CACHE PATH "Path to the eon_dsp header library")

add_subdirectory(JUCE)

# Shared per-target configuration for anything that compiles the emulation.
function(eon_configure_dsp_target name)
    target_include_directories(${name} PRIVATE ${EON_DSP_DIR})
    target_compile_definitions(${name} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        EON_GOD_MODE=1
    )
    target_compile_options(${name} PRIVATE ${EON_FP_FLAGS})
    if(MSVC)
        target_compile_options(${name} PRIVATE /arch:AVX2 /O2 /fp:fast)
    else()
        target_compile_options(${name} PRIVATE -O3 -funroll-loops)
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
endfunction()

function(add_eon_god name product_name code)
    juce_add_plugin(${name}
        COMPANY_NAME "EON AUDIO"
        COMPANY_COPYRIGHT "Copyright (c) 2026 EON AUDIO"
        COMPANY_WEBSITE "https://eon.audio"
        COMPANY_EMAIL "dev@eon.audio"
        BUNDLE_ID "audio.eon.${name}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD ${EON_COPY_PLUGINS}
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
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
        juce::juce_graphics
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
    )
    eon_configure_dsp_target(${name})
    juce_generate_juce_header(${name})
endfunction()

if(EON_BUILD_PLUGINS)
    add_eon_god(Neve1073 "EON 1073 GOD" "E73G")
    add_eon_god(Avalon737 "EON 737 GOD" "E37G")
endif()

if(EON_BUILD_TESTS)
    enable_testing()

    # Header-only smoke test for the pure-DSP core — no JUCE.
    add_executable(dsp_smoke_test Docs/dsp_smoke_test.cpp)
    target_include_directories(dsp_smoke_test PRIVATE ${EON_DSP_DIR})
    target_compile_options(dsp_smoke_test PRIVATE ${EON_TEST_FP_FLAGS})
    if(NOT MSVC)
        target_compile_options(dsp_smoke_test PRIVATE -O2 -Wall -Wextra)
    endif()
    add_test(NAME dsp_smoke COMMAND dsp_smoke_test)

    # ---------------------------------------------------------------------
    # Plugin-level quality harness.
    #
    # A console app that compiles the processors directly, so it can drive real
    # processBlock() calls without building (and installing) the plugin
    # formats. This is what verifies the real-time contracts: no allocation in
    # the steady-state callback, activity detection across all channels, and
    # rate-consistent DSP across quality modes.
    #
    # Build it alone with:  cmake --build <build-dir> --target dsp_quality_test
    # ---------------------------------------------------------------------
    juce_add_console_app(dsp_quality_test PRODUCT_NAME "EON DSP Quality Test")
    target_sources(dsp_quality_test PRIVATE
        Docs/dsp_quality_test.cpp
        Source/Neve1073/PluginProcessor.cpp
        Source/Neve1073/PluginEditor.cpp
        Source/Avalon737/PluginProcessor.cpp
        Source/Avalon737/PluginEditor.cpp
    )
    target_include_directories(dsp_quality_test PRIVATE
        Source
        Source/Shared
    )
    target_link_libraries(dsp_quality_test PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_processors
        juce::juce_graphics
    )
    eon_configure_dsp_target(dsp_quality_test)
    target_compile_definitions(dsp_quality_test PRIVATE EON_QUALITY_HARNESS=1)
    juce_generate_juce_header(dsp_quality_test)
    add_test(NAME dsp_quality COMMAND dsp_quality_test)

    # Offline measurement dumpers (EQ curves + THD -> CSV). Not ctest targets —
    # run manually: ./build/eq_response_dump measurements
    add_executable(eq_response_dump Docs/eq_response_dump.cpp)
    target_include_directories(eq_response_dump PRIVATE ${EON_DSP_DIR})
    target_link_libraries(eq_response_dump PRIVATE juce::juce_dsp)
    if(NOT MSVC)
        target_compile_options(eq_response_dump PRIVATE -O2)
    endif()

    # Alias-foldback measurement (JUCE vs eon oversampler vs 1x). Not a ctest —
    # run manually: ./build/alias_dump measurements
    add_executable(alias_dump Docs/alias_dump.cpp)
    target_include_directories(alias_dump PRIVATE ${EON_DSP_DIR})
    target_link_libraries(alias_dump PRIVATE juce::juce_dsp)
    if(NOT MSVC)
        target_compile_options(alias_dump PRIVATE -O2)
    endif()
endif()
