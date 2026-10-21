#pragma once
#include "abi.h"
#include "nir.hpp"
#include <optional>
#include <string>
#include <vector>

namespace noe {
const noqeri_abi* defaultNoqeriAbi();
std::optional<NirValue> callNoqeriAbi(const noqeri_abi* abi,const std::string& name,const std::vector<NirValue>& args,std::string& error);
const noqeri_abi* defaultHostApi();
std::optional<NirValue> callAbiFunction(const noqeri_abi* abi,const std::string& name,const std::vector<NirValue>& args,std::string& error);
class NirExecutionProfile;
class Interpreter { public: explicit Interpreter(const noqeri_abi* abi=nullptr):host_(abi){} int run(const NirProgram& program); void setExecutionProfile(NirExecutionProfile* profile){profile_=profile;} private: NirValue runFunction(const NirProgram& program,const NirFunction& fn,const std::vector<NirValue>& args); std::string valueToString(const NirValue& value) const; const noqeri_abi* host_=nullptr; NirExecutionProfile* profile_=nullptr; };
} // namespace noe
