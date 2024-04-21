#!/bin/bash

OPENCC_PREFIX=`pkg-config --variable=prefix opencc`

if [[ "$?" != 0 ]] || [[ -z OPENCC_PREFIX ]]; then
    echo "Cannot find opencc prefix"
fi

cat > opencc-profile-name.cpp <<EOF
#include <fcitx-utils/i18n.h>

namespace {
[[maybe_unused]] constexpr std::string_view OpenCCProfileName[] = {
EOF

for profile in `find "$OPENCC_PREFIX"/share/opencc -name "*.json"`; do
    PROFILE_NAME=`jq -r -e ".name" $profile`
    if [[ "$?" != 0 ]]; then
        continue
    fi
    echo "    NC_(\"OpenCC Profile\", \"$PROFILE_NAME\")," >> opencc-profile-name.cpp
done

cat >> opencc-profile-name.cpp <<EOF
};
}
EOF