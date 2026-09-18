cmake_minimum_required(VERSION 3.22)
project(EON_GOD VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    juce_generate_juce_header(${name})
endfunction()

add_eon_god(Neve1073 "EON 1073 GOD" "E73G")
add_eon_god(Avalon737 "EON 737 GOD" "E37G")
