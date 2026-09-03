# Instrument Controller

The Instrument Controller provides a complete runtime for managing and executing instrument workflows within the Falcon ecosystem. It includes a set of executables, libraries, and integration components designed for deployment across Linux and Windows environments.

***

## Installation

```bash
# Or latest 
curl -fsSL https://github.com/falcon-autotuning/instrument-controller/releases/latest/download/install.sh | sudo bash

# Install specific version
curl -fsSL https://github.com/falcon-autotuning/instrument-controller/releases/download/v<VERSION>/install.sh | sudo bash
```

***

## Supported Platforms

| Platform | Supported Environments |
| -------- | ---------------------- |
| Linux    | Bash-compatible shells |

Native PowerShell and `cmd.exe` environments are not supported for direct execution of the installer script.
Note: Windows support has been dropped in favor of linux support only currently.
The Windows support could be developed in the future if there is enough demand.

***

## Installation Directory

By default, the installer places files in the following locations:

* **Linux:**

  ```
  /opt/instrument-controller
  ```

* **Windows (Git Bash / MSYS2):**

  ```
  /c/instrument-controller
  ```

***

### Custom Installation Directory

Set the `FALCON_INSTALL_DIR` environment variable to override the default location:

```bash
FALCON_INSTALL_DIR=/path/to/install \
curl -fsSL https://github.com/falcon-autotuning/instrument-controller/releases/download/v1.0.0/install.sh | bash
```

***

## Post-Installation Setup

After installation, the binaries and libraries must be added to the environment.

### Linux

Add the following to your shell configuration (`~/.bashrc` or `~/.zshrc`):

```bash
export PATH="/opt/instrument-controller:$PATH"
export LD_LIBRARY_PATH="/opt/instrument-controller/lib:$LD_LIBRARY_PATH"
export PKG_CONFIG_PATH="/opt/instrument-controller/lib/pkgconfig:$PKG_CONFIG_PATH"
```

Reload the shell:

```bash
source ~/.bashrc
```

***

### Windows (Git Bash)

```bash
export PATH="/c/instrument-controller/bin:$PATH"
```

For PowerShell sessions:

```powershell
$env:PATH = "C:\instrument-controller\bin;" + $env:PATH
```

***

## Contents

The installation directory contains:

```
<install-dir>/
  bin/        Executables
  lib/        Shared libraries
  include/    Headers for development
  doc/        Documentation and licensing information
```

***

## Requirements

The installation script requires the following tools:

### Required

* `curl`
* `tar` (Linux)
* `unzip` (Windows environments such as Git Bash or MSYS2)

### Optional

* `sudo` (required if installing into system-level directories such as `/opt`)
* `cmake` (if building extensions or integrations)

***

## Verification

To confirm the installation:

```bash
which instrument-worker
```

or:

```bash
instrument-worker --version
```

***

## Uninstallation

Remove the installation directory:

```bash
sudo rm -rf /opt/instrument-controller
```

or on Windows (Git Bash):

```bash
rm -rf /c/instrument-controller
```

***

## Licensing

All third-party dependencies are installed alongside the application. License files are located under:

```
<install-dir>/doc/licenses/
```

A consolidated dependency manifest is also included:

```
<install-dir>/FALCON_DEPENDENCIES.txt
```
