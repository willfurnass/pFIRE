//
//   Copyright 2019 University of Sheffield
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#ifndef CONFIGURATION_HPP
#define CONFIGURATION_HPP

#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <boost/filesystem.hpp>

namespace bf = boost::filesystem;

#include "types.hpp"
#include "exceptions.hpp"

using config_map = std::map<std::string, std::string>;

class ConfigurationBase {
public:

  template <typename T>
  typename std::enable_if<std::is_same<T, bool>::value, T>::type grab(const std::string &key) const
  {
    try
    {
      T val;
      std::istringstream(config.at(key)) >> std::boolalpha >> val;
      return val;
    }
    catch (std::out_of_range &err)
    {
      std::ostringstream errss;
      errss << "Attempt to access non-existent configuration option \"" << key << "\"";
      throw InternalError(errss.str(), __FILE__, __LINE__);
    }
  }

  template <typename T>
  typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value, T>::type
  grab(const std::string &key) const
  {
    try
    {
      return static_cast<T>(std::stoll(config.at(key)));
    }
    catch (std::out_of_range &err)
    {
      std::ostringstream errss;
      errss << "Attempt to access non-existent configuration option \"" << key << "\"";
      throw InternalError(errss.str(), __FILE__, __LINE__);
    }
  }

  template <typename T>
  typename std::enable_if<std::is_floating_point<T>::value && !std::is_same<T, bool>::value, T>::type
  grab(const std::string key) const
  {
    try
    {
      return static_cast<T>(std::stold(config.at(key)));
    }
    catch (std::out_of_range &err)
    {
      std::ostringstream errss;
      errss << "Attempt to access non-existent configuration option \"" << key << "\"";
      throw InternalError(errss.str(), __FILE__, __LINE__);
    }
}

  template <typename T>
  typename std::enable_if<std::is_same<T, std::string>::value, T>::type grab(std::string key) const
  {
    try
    {
      return config.at(key);
    }
    catch (std::out_of_range &err)
    {
      std::ostringstream errss;
      errss << "Attempt to access non-existent configuration option \"" << key << "\"";
      throw InternalError(errss.str(), __FILE__, __LINE__);
    }
  }

  static std::string get_invocation_name(const std::string& argzero);

  void validate_config();

  static const std::string k_stem_token;
  static const std::string k_extension_token;
  static const std::string k_outer_token;
  static const std::string k_inner_token;

protected:
  ConfigurationBase(const int& argc, char const* const* argv);

  config_map config;
  std::vector<std::string> arguments;
  std::string invocation_name;

  static const std::vector<std::string> required_options;
  static const config_map arg_options;
  static const config_map bool_options;
};

#endif // CONFIGURATION_HPP
