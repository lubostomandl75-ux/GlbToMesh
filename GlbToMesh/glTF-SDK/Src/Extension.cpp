// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "Inc/Extension.h"

using namespace Microsoft::glTF;

bool Extension::operator==(const Extension& rhs) const
{
    return IsEqual(rhs);
}

bool Extension::operator!=(const Extension& rhs) const
{
    return !operator==(rhs);
}
