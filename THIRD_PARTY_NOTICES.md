# Third-party software

Vendored in `third_party/`, each under its own license (full text next to the source):

| Library | Version | License | Used by |
|---|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) | 1.3.4 | BSD-2-Clause (`third_party/minhook/LICENSE.txt`) | adapter |
| [miniz](https://github.com/richgel999/miniz) | 3.0.2 | MIT (`third_party/miniz/LICENSE`) | core |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT (`third_party/nlohmann/LICENSE.MIT`) | core |
| [toml++](https://github.com/marzer/tomlplusplus) | 3.4.0 | MIT (`third_party/tomlplusplus/LICENSE`) | core |
| [doctest](https://github.com/doctest/doctest) | 2.4.11 | MIT (`third_party/doctest/LICENSE.txt`) | tests |

Not included in this repository: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
(MIT, by ThirteenAG) is downloaded by `scripts/deploy.sh` at a pinned release and hash.

Grim Dawn is a trademark of Crate Entertainment. This project is unofficial and not affiliated with or
endorsed by Crate Entertainment. No game files are included.
