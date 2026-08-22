# Codex API Switcher

Codex API Switcher is a Windows desktop utility written in C++20 with raylib and CMake. It provides a visual interface for managing multiple Codex `model_provider` profiles and applying the selected profile to the local `config.toml`.

## Features

- Dark, resizable desktop GUI.
- A fixed, non-removable `Default` provider for Codex's built-in `openai` provider and ChatGPT account login.
- Add, edit, select, and remove custom providers.
- Microsoft YaHei regular UI font with SDF and high-DPI rendering.
- `Ctrl+S` to save and apply changes; `F2` to reveal the configuration file on Windows.
- Stores API key environment variable names only; never stores API key values.
- Preserves unrelated TOML tables when updating managed configuration entries.

## Default Provider

`Default` is a built-in virtual entry. It does not correspond to a `[model_providers.openai]` table. This follows Codex's default provider behavior:

- Saving `Default` removes the top-level `model_provider` setting.
- Codex then falls back to its built-in `openai` provider and ChatGPT account login.
- The application never creates or overwrites `[model_providers.openai]`.
- `Default` is always listed first and cannot be deleted. Its model field can still update the top-level `model` setting.

Custom providers use a configuration such as:

```toml
model_provider = "my-provider"

[model_providers.my-provider]
name = "My Provider"
base_url = "https://example.com/v1"
env_key = "MY_PROVIDER_API_KEY"
wire_api = "responses"
```

`env_key` is only the name of an environment variable. Configure the actual secret in your operating system environment. The application does not write the secret to the repository or to `config.toml`.

## Requirements

- Windows 10 or Windows 11
- Visual Studio 2026 with the Desktop development with C++ workload
- CMake 3.23 or newer
- A raylib 6.0 source checkout for building from source

The published Windows package already contains a statically linked raylib
build. End users do not need to install raylib.

The default CMake preset expects raylib to be next to this project:

```text
GitHub/
├─ Codex API Switcher/
└─ raylib/
```

You can also provide another raylib location with `RAYLIB_SOURCE_DIR`.

## Build with Visual Studio 2026

Open the project directory or `CMakePresets.json` in Visual Studio 2026 and select:

- Configure preset: `vs2026-x64`
- Build preset: `vs2026-release`

From a Visual Studio Developer PowerShell:

```powershell
cmake --preset vs2026-x64
cmake --build --preset vs2026-release
```

The Release executable is written to:

```text
build-vs2026/Release/Codex API Switcher.exe
```

If raylib is located elsewhere:

```powershell
cmake -S . -B build-vs2026 `
  -G "Visual Studio 18 2026" -A x64 `
  -DRAYLIB_SOURCE_DIR=C:/path/to/raylib
cmake --build build-vs2026 --config Release
```

## Usage

1. Launch `Codex API Switcher.exe`.
2. The application reads `%USERPROFILE%\.codex\config.toml` by default.
3. Select `Default` to restore Codex's built-in ChatGPT login provider.
4. Select `Add provider` to create a custom profile and enter its model, base URL, and environment variable name.
5. Select `Use this provider`, then select `Save & Apply` to write the configuration.

Back up `%USERPROFILE%\.codex\config.toml` before making the first change to a real configuration.

## Configuration Example

[`config.example.toml`](config.example.toml) is a sanitized example and does not contain a real account or secret. When using the default ChatGPT login provider, `model_provider` should be omitted:

```toml
model = "gpt-5"
```

For a custom provider, copy the example and replace the placeholder provider values. Never commit a real API key, OAuth token, password, or personal configuration file.

## Security Notes

This project is a local configuration editor, not a Codex login client:

- It does not store ChatGPT cookies, OAuth tokens, refresh tokens, or account passwords.
- It does not read the actual API key value; it stores only the environment variable name.
- It does not upload configuration data to a third-party server or include a built-in API key.
- `Default` relies on the ChatGPT login state managed by Codex itself.

Only source code, build entry points, license files, third-party notices, and
sanitized configuration examples should be committed to a public repository.
Build trees, `config.toml`, `.env` files, certificates, and key files are
covered by `.gitignore`.

## Third-Party Components and Licenses

The application statically links raylib 6.0.0. The raylib build includes GLFW
3.4 and several permissively licensed components, including stb, miniaudio,
dr_mp3, dr_flac, dr_wav, cgltf, and cgltf_write. Their copyright notices and
license terms are included in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)
and are also included in the release ZIP.

These third-party licenses apply only to the corresponding bundled components.
They do not replace or modify this project's license in [`LICENSE`](LICENSE).

## Project Structure

```text
.
├─ src/
│  ├─ config_manager.cpp  # TOML loading and saving
│  ├─ config_manager.h
│  └─ main.cpp            # raylib GUI and interaction
├─ config.example.toml    # sanitized configuration example
├─ CMakeLists.txt
├─ CMakePresets.json
├─ LICENSE
└─ THIRD_PARTY_NOTICES.md
```

## License

The Codex API Switcher project is distributed under the GNU Affero General Public
License, version 3 (AGPLv3), in [`LICENSE`](LICENSE).
Third-party components are distributed under their respective licenses listed
in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
