#!/usr/bin/env python3
# Print NI_XCODER_REVISION from source/ni_defs.h, e.g. "5806tCr2"

import os
import re

path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'source', 'ni_defs.h')
with open(path, encoding='utf-8') as f:
    print(re.search(r'#define\s+NI_XCODER_REVISION\s+"([^"]+)"', f.read()).group(1))
