# Major operation: emit a machine-readable, hash-bound programming manifest.
# Invoked by the target's post-build step; no device or option-byte access occurs.
if(NOT EXISTS "${IMAGE}" OR NOT PROFILE MATCHES "^(normal|bringup)$")
    message(FATAL_ERROR "Invalid Atlas image/profile for firmware manifest")
endif()
get_filename_component(image_dir "${IMAGE}" DIRECTORY)
get_filename_component(image_base "${IMAGE}" NAME_WE)
set(binary "${image_dir}/${image_base}.bin")
set(hex "${image_dir}/${image_base}.hex")
file(SHA256 "${IMAGE}" elf_hash)
file(SHA256 "${binary}" binary_hash)
file(SHA256 "${hex}" hex_hash)
file(SIZE "${binary}" binary_size)
if(PROFILE STREQUAL "bringup" AND binary_size GREATER 1048576)
    message(FATAL_ERROR "Bringup image must fit flash bank 1; review the DFU erase contract")
endif()
file(WRITE "${image_dir}/${image_base}.manifest.json"
    "{\n  \"schema\": 1,\n  \"profile\": \"${PROFILE}\",\n  \"target\": \"STM32H743ZIT6\",\n  \"build_type\": \"${BUILD_TYPE}\",\n  \"flash_base\": \"0x08000000\",\n  \"binary\": \"${image_base}.bin\",\n  \"binary_bytes\": ${binary_size},\n  \"binary_sha256\": \"${binary_hash}\",\n  \"hex\": \"${image_base}.hex\",\n  \"hex_sha256\": \"${hex_hash}\",\n  \"elf_sha256\": \"${elf_hash}\"\n}\n")
