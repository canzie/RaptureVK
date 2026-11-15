# rapture_set_warnings(<target>)
#
# The warning set every Rapture target builds with, as errors, in every configuration.
function(rapture_set_warnings TARGET)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4 /WX)
    else()
        target_compile_options(${TARGET} PRIVATE -Wall -Wextra -pedantic -Werror)
    endif()
endfunction()
