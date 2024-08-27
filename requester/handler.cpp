#include "handler.hpp"
#include <iostream>

namespace pldm {
namespace requester {
std::ostream &operator<<(std::ostream &os, const RequestKey &obj) {
  os << "{\n\teid: " << int(obj.eid) << "\n\tinstanceId: " << int(obj.instanceId)
     << "\n\ttype: " << int(obj.type) << "\n\tcommand: " << int(obj.command) << "\n}\n";
  return os;
}
} // namespace requester
} // namespace pldm