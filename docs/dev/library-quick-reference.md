# Library Management Quick Reference

## Files

- **`arduino-libraries.txt`** - Configuration file listing all required libraries
- **`library.sh`** - Command-line tool for managing libraries

## Commands

```bash
# Search for libraries
./library.sh search <keyword>

# Add a library (installs + adds to config)
./library.sh add <library_name>
./library.sh add <library_name@version>

# Remove from config (doesn't uninstall)
./library.sh remove <library_name>

# List configured libraries
./library.sh list

# Install all configured libraries
./library.sh install

# Show currently installed libraries
./library.sh installed
```

## Workflow

1. **Find a library:**
   ```bash
   ./library.sh search bme280
   ```

2. **Add to project:**
   ```bash
   ./library.sh add "Adafruit BME280 Library"
   ```

3. **Verify:**
   ```bash
   ./library.sh list
   ```

4. **Commit:**
   ```bash
   git add arduino-libraries.txt
   git commit -m "Add BME280 sensor library"
   ```

5. **CI/CD automatically installs it** via `setup.sh`

## Examples

```bash
# Search for sensor libraries
./library.sh search "adafruit sensor"

# Add with specific version
./library.sh add "Adafruit BME280 Library@2.2.2"

# Add library with spaces in name
./library.sh add "Adafruit GFX Library"

# Remove a library from config
./library.sh remove WiFi

# Reinstall all configured libraries
./library.sh install
```

## Integration

- **Local Setup**: `./setup.sh` reads `arduino-libraries.txt` and installs all libraries
- **CI/CD**: GitHub Actions runs `./setup.sh`, automatically installing libraries
- **Version Control**: `arduino-libraries.txt` tracks dependencies across team/environments
- **Initial State**: Template ships with a small set of libraries already configured; customize `arduino-libraries.txt` as needed

## Format of arduino-libraries.txt

```
# Comments start with #
LibraryName
LibraryName@1.2.3
"Library With Spaces"
```
