cmake_minimum_required(VERSION 3.22)

set(PHOTOPAINTER_REQUIRED_IDF_COMMIT
    "5e6f53cdb31fe5708eae3f55af9737be2822db22")
execute_process(
    COMMAND git -C "$ENV{IDF_PATH}" rev-parse HEAD
    RESULT_VARIABLE PHOTOPAINTER_IDF_GIT_RESULT
    OUTPUT_VARIABLE PHOTOPAINTER_IDF_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT PHOTOPAINTER_IDF_GIT_RESULT EQUAL 0 OR
   NOT PHOTOPAINTER_IDF_COMMIT STREQUAL PHOTOPAINTER_REQUIRED_IDF_COMMIT)
    message(FATAL_ERROR
        "photopainter-host requires ESP-IDF commit "
        "${PHOTOPAINTER_REQUIRED_IDF_COMMIT}")
endif()

# elf_loader 1.3.3's project include invokes its symbol generator through the
# legacy Python_EXECUTABLE variable.  ESP-IDF 6 passes the interpreter as
# PYTHON instead; bridge the names in this project so a pristine Registry
# component works without changing its files or executable bits.
if(NOT Python_EXECUTABLE AND PYTHON)
    set(Python_EXECUTABLE "${PYTHON}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(photopainter_host)
