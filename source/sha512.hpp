//! SHA-512, for checking a downloaded pack against the checksum its source lists
//! (Modrinth publishes SHA-512 per file). FIPS 180-4; the round constants are the
//! fractional parts of the cube roots of the first 80 primes.
#pragma once

#include <cstdint>
#include <string>

namespace launcher
{

/// The SHA-512 of `data`, lowercase hex (128 characters).
std::string sha512Hex(const std::string& data);

/// The SHA-512 of a file, read in blocks; empty when it can't be read.
std::string sha512FileHex(const std::string& path);

} // namespace launcher
