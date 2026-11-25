# Third-party dependencies

The following dependencies are downloaded during setup and are not vendored in the source repository.

| Dependency | Source | License |
| --- | --- | --- |
| Dear ImGui v1.92.8 | https://github.com/ocornut/imgui | MIT |
| SDL3 | https://github.com/libsdl-org/SDL | zlib |
| SQLite | https://www.sqlite.org/copyright.html | Public domain dedication; retain the port's bundled notice |
| vcpkg build tooling | https://github.com/microsoft/vcpkg | MIT |

SDL3 and SQLite versions are selected by the baseline committed in `Dependencies/vcpkg.json`. Dear ImGui is compiled into the host; SDL3 and SQLite are deployed as DLLs.

`Aries_Host/deploy_runtime.ps1` copies the actual installed Dear ImGui, SDL3, and SQLite notices to `bin/<Configuration>/licenses`, together with the project's GPLv3 text. Include that directory when distributing a build. These notices do not replace the corresponding-source obligations for Aries Reborn itself.
