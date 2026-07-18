# Embeds a file as a C byte array header.
# Usage: cmake -DIN=<input> -DOUT=<output.h> -DSYMBOL=<name> -P embed_file.cmake
file(READ "${IN}" _hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
string(LENGTH "${_hex}" _hexlen)
math(EXPR _len "${_hexlen} / 2")
file(WRITE "${OUT}" "// Generated from ${IN} — do not edit.
#pragma once
static const unsigned char ${SYMBOL}[] = { ${_bytes} };
static const unsigned int ${SYMBOL}_len = ${_len};
")
