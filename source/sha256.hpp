//! SHA-256, for checking the game download against the checksum its release publishes.
//! FIPS 180-4; the round constants are the fractional parts of the cube roots of the first 64 primes,
//! the starting state the square roots of the first 8.
#pragma once

#include <cstdint>
#include <string>

namespace launcher
{

/// The SHA-256 of a file, read in blocks; empty when it can't be read.
std::string sha256FileHex(const std::string& path);

/// The SHA-256 of `data`, lowercase hex (64 characters).
std::string sha256Hex(const std::string& data);

} // namespace launcher
