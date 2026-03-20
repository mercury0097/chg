# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "2.0.3")

# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

# 🛡️ 自动检查并应用 VADNet1 补丁（防止 managed_components 修改丢失）
set(PATCH_CHECK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/managed_components/espressif__esp-sr/src/model_path.c")
set(PATCH_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/apply_vadnet1_fix.sh")

if(EXISTS "${PATCH_CHECK_FILE}")
    # 检查补丁标记是否存在
    file(READ "${PATCH_CHECK_FILE}" PATCH_FILE_CONTENT)
    string(FIND "${PATCH_FILE_CONTENT}" "[PATCH] Clear old static_srmodels cache" PATCH_FOUND)
    
    if(PATCH_FOUND EQUAL -1)
        message(WARNING "⚠️  检测到 VADNet1 补丁缺失，正在自动应用...")
        if(EXISTS "${PATCH_SCRIPT}")
            execute_process(
                COMMAND bash "${PATCH_SCRIPT}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                RESULT_VARIABLE PATCH_RESULT
                OUTPUT_VARIABLE PATCH_OUTPUT
                ERROR_VARIABLE PATCH_ERROR
            )
            if(PATCH_RESULT EQUAL 0)
                message(STATUS "✅ VADNet1 补丁已自动应用成功")
            else()
                message(WARNING "⚠️  补丁应用失败：${PATCH_ERROR}")
            endif()
        else()
            message(WARNING "⚠️  补丁脚本不存在：${PATCH_SCRIPT}")
        endif()
    else()
        message(STATUS "✅ VADNet1 补丁已存在，无需重新应用")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(xiaozhi)

