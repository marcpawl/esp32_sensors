#include "espidf_cpp/nvs.hpp"

// TODO(AI!) add test cases for espidf_cpp::Nvs
// defined in components/espidfcpp/libespidfcpp/include/espidf_cpp/nvs.hpp
//
// Include test write and read for:
//  - uint8
//  -  uint16
//  -  uint32
//  - int8
//  -  int16
//  - int32
//  - float
//  - static_string
//
// Read tests should include value not present returns default value
//
// static_string should include:
//    -  empty string
//    - string of maximum length,
//    - written string bigger than size of string being read in.
//
// Two Nvs instances opened at the same time, what is the behaviour
//
// static_assert that Nvs<NvsMode::Read> has  no write functions
