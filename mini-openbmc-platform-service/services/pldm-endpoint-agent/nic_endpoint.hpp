#pragma once

#include "libs/pldm/pldm_common.hpp"
#include "libs/pldm/pldm_type2.hpp"

namespace service {

class NicEndpoint {
  public:
    NicEndpoint();
    pldm::Message handle(const pldm::Message& request);
    void setFault(const std::string& fault);

  private:
    pldm::Type2Responder responder_;
};

} // namespace service
