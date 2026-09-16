// =====================================================================
//  src/libhobbycad/naming.cpp — what a valid object name is
//  Repository: ayourk/hobbycad
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
#include "hobbycad/naming.h"

#include <cctype>

namespace hobbycad {

bool isValidObjectName(const std::string& name, std::string* reason)
{
    auto fail = [reason](const char* why) {
        if (reason) *reason = why;
        return false;
    };

    if (name.empty()) {
        return fail("A name cannot be empty.");
    }

    // Leading or trailing spaces are almost always a paste accident, and
    // two names differing only by an invisible space are indistinguishable
    // on screen.
    if (std::isspace(static_cast<unsigned char>(name.front()))
        || std::isspace(static_cast<unsigned char>(name.back()))) {
        return fail("A name cannot begin or end with a space.");
    }

    // The CLI prompt writes "sketch *Profile>" while a sketch is being
    // edited. That marker is only unambiguous while no name can start with
    // it; otherwise a sketch called "*Profile" and one being edited look
    // identical.
    if (name.front() == kEditMarker) {
        return fail("A name cannot start with '*'. That marks a sketch "
                    "that is being edited.");
    }

    // Control characters would corrupt the prompt and any exported script.
    for (const char c : name) {
        if (std::iscntrl(static_cast<unsigned char>(c))) {
            return fail("A name cannot contain control characters.");
        }
    }

    return true;
}

}  // namespace hobbycad
