# Third-party notices and provenance

## libmseed

Upstream: https://github.com/EarthScope/libmseed
Version: 3.5.4; base commit:
`d6e6ad306de5b7bdde072e5a5e3d58ddcc11abd8`.
Source: https://github.com/EarthScope/libmseed/tree/d6e6ad306de5b7bdde072e5a5e3d58ddcc11abd8

Copyright: Chad Trabant, EarthScope Data Services, and other upstream
contributors, with years and details retained in each source file.
License: Apache License 2.0, reproduced in `libmseed/LICENSE`.

This is a modified fork. Andrei Akimov's experimental integration changes
`libmseed/pack.c`, `unpack.c`, `lookup.c` and `libmseed.h`: encoding constants,
description and record-level packing/unpacking dispatch. Modification notices
are present in those files. `tracelist.c` retains upstream behavior.
`integration.patch` records the integration relative to the stated base.
No upstream NOTICE file was present in the bundled libmseed source tree.
All existing upstream attribution remains intact.

## FLAC / libFLAC

Upstream: https://xiph.org/flac/ and https://github.com/xiph/flac
Bundled source: `flac-1.5.0.tar.xz`, version 1.5.0, unmodified.
The project adapter uses public libFLAC APIs; it does not change libFLAC source.

Copyright (C) 2000-2009 Josh Coalson
Copyright (C) 2011-2025 Xiph.Org Foundation
Additional notices and years are retained in the source archive.

The libFLAC codec library uses Xiph.Org's BSD-style license, reproduced
verbatim in `COPYING.Xiph` (also inside the FLAC source archive).
The bundled archive is a multi-license source distribution: other programs,
documentation, tests and build support carry their own license notices.
See its README.md, COPYING.Xiph, COPYING.GPL, COPYING.LGPL, COPYING.FDL
and individual files. The build may build FLAC utilities in addition to the
library; do not describe the entire FLAC archive as BSD-only.

## Project additions and combined distribution

Copyright (C) 2026 Andrei Akimov, for original project contributions.
Adapter, tools, tests and integration additions retain GPL-2.0-or-later notices.
The combined Apache-2.0/GPL build is distributed under GPLv3, selecting the
existing later-version permission. See LICENSE and COPYING.GPL3.

License compatibility reference:
https://www.apache.org/licenses/GPL-compatibility

Names of upstream projects, authors, EarthScope, Xiph.Org and OpenAI do not
imply endorsement of this experimental extension. Credit and development
assistance are described in AUTHORS.md. Benchmark waveform data are from
EarthScope FDSN services and are not software-authorship contributions.
