/* Single source of truth for SwiftII's release identity: version, year,
 * and copyright.
 *
 * Bump these when cutting a release (the year once a year). Both the REPL
 * banner (via config.h, which includes this header) and the build tooling
 * read the version: `make release` stages the disk images under
 * releases/v<version>/.
 */
#ifndef SWIFTII_VERSION_H
#define SWIFTII_VERSION_H

#define SWIFTII_VERSION   "1.0.0"
#define SWIFTII_YEAR      "2026"
#define SWIFTII_COPYRIGHT "Copyright (c) " SWIFTII_YEAR " Yeo Kheng Meng"

#endif /* SWIFTII_VERSION_H */
