# Third-party components

The receiver links the following unmodified upstream libraries statically. The project build restores their source archives from the URLs and SHA-256 values recorded in `dependencies.json`.

| Component | Version | Source | License |
| --- | --- | --- | --- |
| SRT | 1.5.4 | https://github.com/Haivision/srt/tree/v1.5.4 | Mozilla Public License 2.0 |
| Mbed TLS | 3.6.5 | https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.5 | Apache License 2.0 option of its dual license |

SRT copyright: Haivision Systems Inc. and its contributors. Mbed TLS and bundled cryptographic implementations retain the copyright notices in their source files. These components are not covered by the project's MIT license.

`build.ps1` packages the upstream license texts in `dist/<architecture>/<configuration>/licenses/`. Keep those texts and this document with distributed binaries. SRT source is available from the versioned source link above and the archive identified by the manifest. If you change MPL-covered dependency files, preserve their notices and make the corresponding covered source available under the MPL.

The BonDriver interface declarations specify the host ABI: virtual-function order and Windows data types must remain compatible. This repository does not include a TVTest or EDCB implementation.
