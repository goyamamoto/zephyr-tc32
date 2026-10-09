# SPDX-License-Identifier: Apache-2.0

# TC32 code is built by a mainstream clang for ARMv4T Thumb (triple
# thumbv4t-none-eabi, cmake/toolchain/host/llvm/target.cmake): the assembly
# sources, written for Telink's assembler, go through tc32asm2thumb.py, and
# thumb2tc32.py writes the TC32 image from the link. Both tools are in
# TC32_THUMB_TOOLS: TC32-devtools' compiler/ folder, $TC32_DEVTOOLS/compiler
# when that variable is set, else the checkout next to the workspace
# (ZEPHYR_BASE/../../TC32-devtools).
#
# TC32_DIRECT=ON (with TC32_THUMB=ON) selects the direct path: llvm-tc32's
# clang writes TC32 machine code itself (-mcpu=tc32 -mthumb, the same code
# generation as ARMv4T Thumb), ld.lld links it, and elf2bin.py (also in
# TC32_THUMB_TOOLS) writes the image without re-encoding. Each assembly
# source then is its standard-syntax twin, <name>.ual.S next to <name>.S
# (tc32_asm_source), assembled as it is.
if(NOT TC32_THUMB)
  message(FATAL_ERROR "TC32 builds need -DTC32_THUMB=ON (clang for ARMv4T Thumb)")
endif()
if(TC32_DIRECT)
  set(TC32_CPU tc32)
else()
  set(TC32_CPU arm7tdmi)
endif()
if(DEFINED ENV{TC32_DEVTOOLS})
  set(tc32_devtools $ENV{TC32_DEVTOOLS})
else()
  set(tc32_devtools ${ZEPHYR_BASE}/../../TC32-devtools)
endif()
set(TC32_THUMB_TOOLS ${tc32_devtools}/compiler CACHE PATH "tc32asm2thumb.py and thumb2tc32.py (TC32-devtools compiler/)")
set_property(GLOBAL PROPERTY PROPERTY_OUTPUT_FORMAT "elf32-littlearm")
if(TC32_DIRECT)
  # The compiler must know the core: one without it stops here rather than
  # in the first compile.
  execute_process(COMMAND ${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}
    -mcpu=tc32 -mthumb -mfloat-abi=soft -x c -S -o - -
    INPUT_FILE /dev/null OUTPUT_QUIET ERROR_VARIABLE cpu_err RESULT_VARIABLE cpu_rc)
  if(NOT cpu_rc EQUAL 0)
    message(FATAL_ERROR "TC32_DIRECT: ${CMAKE_C_COMPILER} does not take -mcpu=tc32; "
      "the direct path needs llvm-tc32.\n${cpu_err}")
  endif()
endif()
zephyr_compile_options(-mcpu=${TC32_CPU} -mthumb -mfloat-abi=soft)
zephyr_ld_options(-mcpu=${TC32_CPU} -mthumb)

# Assembly sources written for Telink's assembler, translated to Thumb.
# tc32_asm_source(var src) sets var to the file to compile, translated when
# CMake runs (a change to the source or
# the translator runs CMake again), so any target can take it;
# tc32_asm_sources(src...) adds them to the current Zephyr library.
#
# A source may have a twin in standard syntax (unified Thumb, the TC32-only
# instructions as .inst.n), <name>.ual.S next to <name>.S. The direct path
# (TC32_DIRECT) compiles the twin and requires it; the Thumb path compiles
# the translation. On both paths the build assembles the translation and the
# twin with the same flags and compares the two objects (asm_twin_check.py:
# the bytes of every allocated section, the relocations by symbol, the
# symbols, the build attributes), so the image fails to build when the two
# files no longer give the same code and data.
function(tc32_asm_source var src)
  get_filename_component(abs ${src} ABSOLUTE)
  get_filename_component(name ${src} NAME_WE)
  get_filename_component(dir ${abs} DIRECTORY)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/${name}.thumb.S)
  set(tool ${TC32_THUMB_TOOLS}/tc32asm2thumb.py)
  execute_process(COMMAND ${PYTHON_EXECUTABLE} ${tool} ${abs} ${out}
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "tc32asm2thumb.py ${src} failed")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${abs} ${tool})
  set(twin ${dir}/${name}.ual.S)
  if(EXISTS ${twin})
    tc32_asm_twin_check(${name} ${out} ${twin})
  elseif(TC32_DIRECT)
    message(FATAL_ERROR "TC32_DIRECT: ${src} has no standard-syntax twin ${twin}")
  endif()
  if(TC32_DIRECT)
    set(${var} ${twin} PARENT_SCOPE)
  else()
    set(${var} ${out} PARENT_SCOPE)
  endif()
endfunction()
# tc32_asm_twin_check(name translation twin): an object library assembling
# both files with the flags of every Zephyr library, and a step of the default
# build that compares the two objects.
function(tc32_asm_twin_check name translation twin)
  set(lib tc32_asm_twin_${name})
  if(TARGET ${lib})
    message(FATAL_ERROR "two assembly sources named ${name}: their twin checks would share ${lib}")
  endif()
  add_library(${lib} OBJECT ${translation} ${twin})
  target_link_libraries(${lib} PRIVATE zephyr_interface)
  add_dependencies(${lib} zephyr_generated_headers)
  set(tool ${TC32_THUMB_TOOLS}/asm_twin_check.py)
  set(stamp ${CMAKE_CURRENT_BINARY_DIR}/${lib}.ok)
  add_custom_command(OUTPUT ${stamp}
    COMMAND ${PYTHON_EXECUTABLE} ${tool} "$<TARGET_OBJECTS:${lib}>"
    COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
    DEPENDS ${lib} $<TARGET_OBJECTS:${lib}> ${tool}
    COMMAND_EXPAND_LISTS
    COMMENT "Comparing ${name}.S (translated) with its twin ${name}.ual.S"
  )
  add_custom_target(${lib}_check ALL DEPENDS ${stamp})
endfunction()
function(tc32_asm_sources)
  foreach(src ${ARGN})
    tc32_asm_source(file ${src})
    zephyr_library_sources(${file})
  endforeach()
endfunction()
# No jump tables: the LLVM TC32 backend dispatches them with "tadd rX, pc",
# assuming the PC it reads is word-aligned; ARM Thumb reads the instruction
# address + 4, and which the TC32 reads is not documented. Without jump
# tables no instruction reads PC except literal loads.
zephyr_compile_options(-fno-jump-tables)
# No merging of loads into ldm/stm after register allocation: it makes "ldm
# rN, {..., rN, ...}" (base register in the list, no writeback), and how the
# TC32 treats that form is not documented. Block copies still use ldm/stm
# with writeback.
zephyr_compile_options("SHELL:-mllvm -arm-load-store-opt=false")
# Identical code folding in the link (arch/tc32/Kconfig, TC32_ICF).
if(CONFIG_TC32_ICF_SAFE)
  zephyr_ld_options(-Wl,--icf=safe)
elseif(CONFIG_TC32_ICF_ALL)
  zephyr_ld_options(-Wl,--icf=all)
endif()

# Returns with pop {..., pc} (TC32_POP_PC_RETURNS): the noarm target feature
# on every C compile. tc32_classic_returns(file...) names compile units that
# keep the classic return, pop {...}; pop {rN}; bx rN: each gets -noarm as a
# source option, which the command line places after the global +noarm (a
# source's options are not de-duplicated, so both -Xclang stay), and of two
# settings of a target feature the compiler takes the last. The files
# can be sources of any target (a Zephyr library, a module's library, the
# application): once every directory has been processed, the sources of every
# target are matched against them (tc32_apply_classic_returns).
if(CONFIG_TC32_POP_PC_RETURNS)
  # The compiler must honour the feature (a clang without it ignores it
  # without a message) and take a later -noarm over it: a function that
  # saves lr, compiled with +noarm, returns with pop {..., pc}; with +noarm
  # then -noarm, through bx.
  set(probe ${CMAKE_CURRENT_BINARY_DIR}/tc32_pop_pc_probe.c)
  file(WRITE ${probe} "int tc32_probe_callee(int x);\n\nint tc32_probe(int x)\n{\n"
    "\treturn tc32_probe_callee(x) + tc32_probe_callee(x + 1);\n}\n")
  set(probe_cmd ${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}
    -mcpu=${TC32_CPU} -mthumb -mfloat-abi=soft -Os -S -o - ${probe}
    -Xclang -target-feature -Xclang +noarm)
  execute_process(COMMAND ${probe_cmd}
    OUTPUT_VARIABLE pop_asm ERROR_VARIABLE pop_err RESULT_VARIABLE pop_rc)
  execute_process(COMMAND ${probe_cmd} -Xclang -target-feature -Xclang -noarm
    OUTPUT_VARIABLE bx_asm ERROR_VARIABLE bx_err RESULT_VARIABLE bx_rc)
  if(NOT pop_rc EQUAL 0 OR NOT pop_asm MATCHES "pop[ \t]+{[^}\n]*pc}")
    message(FATAL_ERROR "TC32_POP_PC_RETURNS: ${CMAKE_C_COMPILER} does not return "
      "with pop {..., pc} under -target-feature +noarm: it lacks the noarm "
      "feature (ignored without a message) or failed. Use a clang that has "
      "the feature, or set CONFIG_TC32_POP_PC_RETURNS=n.\n${pop_err}")
  endif()
  if(NOT bx_rc EQUAL 0 OR bx_asm MATCHES "pop[ \t]+{[^}\n]*pc}"
     OR NOT bx_asm MATCHES "bx[ \t]")
    message(FATAL_ERROR "TC32_POP_PC_RETURNS: with ${CMAKE_C_COMPILER}, "
      "-target-feature -noarm after +noarm does not give the classic return "
      "(pop {...}; pop {rN}; bx rN): tc32_classic_returns() would not work.\n${bx_err}")
  endif()
  zephyr_compile_options("$<$<COMPILE_LANGUAGE:C>:SHELL:-Xclang -target-feature -Xclang +noarm>")
elseif(TC32_DIRECT)
  # The core tc32 has the noarm feature itself: without the option every C
  # compile gets -noarm, so that the code keeps the classic return, as on the
  # Thumb path.
  set(probe ${CMAKE_CURRENT_BINARY_DIR}/tc32_pop_pc_probe.c)
  file(WRITE ${probe} "int tc32_probe_callee(int x);\n\nint tc32_probe(int x)\n{\n"
    "\treturn tc32_probe_callee(x) + tc32_probe_callee(x + 1);\n}\n")
  execute_process(COMMAND ${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}
    -mcpu=${TC32_CPU} -mthumb -mfloat-abi=soft -Os -S -o - ${probe}
    -Xclang -target-feature -Xclang -noarm
    OUTPUT_VARIABLE bx_asm ERROR_VARIABLE bx_err RESULT_VARIABLE bx_rc)
  if(NOT bx_rc EQUAL 0 OR bx_asm MATCHES "pop[ \t]+{[^}\n]*pc}"
     OR NOT bx_asm MATCHES "bx[ \t]")
    message(FATAL_ERROR "TC32_DIRECT: with ${CMAKE_C_COMPILER}, -mcpu=tc32 and "
      "-target-feature -noarm do not give the classic return "
      "(pop {...}; pop {rN}; bx rN).\n${bx_err}")
  endif()
  zephyr_compile_options("$<$<COMPILE_LANGUAGE:C>:SHELL:-Xclang -target-feature -Xclang -noarm>")
endif()
function(tc32_classic_returns)
  foreach(src ${ARGN})
    get_filename_component(abs ${src} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY TC32_CLASSIC_RETURN_SOURCES ${abs})
  endforeach()
endfunction()
function(tc32_apply_classic_returns)
  get_property(files GLOBAL PROPERTY TC32_CLASSIC_RETURN_SOURCES)
  if(NOT files)
    return()
  endif()
  set(dirs ${CMAKE_SOURCE_DIR})
  set(units)
  while(dirs)
    list(POP_FRONT dirs dir)
    get_property(sub DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    list(APPEND dirs ${sub})
    get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target ${targets})
      get_target_property(type ${target} TYPE)
      if(type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "UTILITY")
        continue()
      endif()
      get_target_property(sources ${target} SOURCES)
      get_target_property(base ${target} SOURCE_DIR)
      foreach(src ${sources})
        if(src MATCHES "^\\$<")
          continue()
        endif()
        get_filename_component(abs ${src} ABSOLUTE BASE_DIR ${base})
        if(abs IN_LIST files)
          if(CONFIG_TC32_POP_PC_RETURNS)
            set_property(SOURCE ${abs} TARGET_DIRECTORY ${target} APPEND PROPERTY
              COMPILE_OPTIONS -Xclang -target-feature -Xclang -noarm)
          endif()
          # The same units are compiled without the address-significance table (-fno-addrsig):
          # ld.lld's --icf=safe then counts every symbol they define or reference as
          # address-significant and folds none of them, so their code and their call targets stay
          # as compiled. --icf=all folds code whatever the table says; of these units only the
          # read-only data then stays unfolded.
          if(CONFIG_TC32_ICF_SAFE OR CONFIG_TC32_ICF_ALL)
            set_property(SOURCE ${abs} TARGET_DIRECTORY ${target} APPEND PROPERTY
              COMPILE_OPTIONS -fno-addrsig)
          endif()
          list(APPEND units "${target}:${abs}")
        endif()
      endforeach()
    endforeach()
  endwhile()
  list(LENGTH units n)
  message(STATUS "Frozen units (classic returns, unfolded): ${n} compile units")
  foreach(unit ${units})
    message(STATUS "  ${unit}")
  endforeach()
endfunction()
if(CONFIG_TC32_POP_PC_RETURNS OR CONFIG_TC32_ICF_SAFE OR CONFIG_TC32_ICF_ALL)
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL tc32_apply_classic_returns)
endif()

add_subdirectory(core)
zephyr_include_directories(include)
zephyr_include_directories(${ZEPHYR_BASE}/soc)
