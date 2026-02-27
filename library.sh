#!/bin/bash

# Arduino Library Management Script
# Helps add, remove, and list libraries for the ESP32 project

set -e

# Load common configuration and functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/config.sh"

# Get arduino-cli path
ARDUINO_CLI=$(find_arduino_cli)

# Configuration file for libraries
LIBRARIES_FILE="$SCRIPT_DIR/arduino-libraries.txt"

# Colors are already defined in config.sh

show_usage() {
    cat << EOF
Arduino Library Management for ESP32 Template

Usage:
  ./library.sh search <keyword>    Search for libraries
  ./library.sh add <library>       Add library to arduino-libraries.txt and install it
  ./library.sh remove <library>    Remove library from arduino-libraries.txt
  ./library.sh list                List configured libraries
  ./library.sh install             Install all libraries from arduino-libraries.txt
  ./library.sh installed           Show currently installed libraries

Examples:
  ./library.sh search mqtt
  ./library.sh add PubSubClient@2.8
  ./library.sh add "Adafruit GFX Library"
  ./library.sh remove ArduinoJson
  ./library.sh list

EOF
}

check_arduino_cli() {
    # ARDUINO_CLI may be an absolute path OR a command name.
    if command -v "$ARDUINO_CLI" >/dev/null 2>&1; then
        return 0
    fi
    if [[ -x "$ARDUINO_CLI" ]]; then
        return 0
    fi

    echo -e "${RED}Error: arduino-cli not found or not executable: $ARDUINO_CLI${NC}"
    echo "PATH=\"$PATH\""
    echo "Tip: run ./setup.sh (local install) or export ARDUINO_CLI=/full/path/to/arduino-cli"
    exit 1
}

search_library() {
    local keyword="$1"
    if [ -z "$keyword" ]; then
        echo -e "${RED}Error: Please provide a search keyword${NC}"
        exit 1
    fi
    
    check_arduino_cli
    echo -e "${CYAN}Searching for libraries matching '$keyword'...${NC}"
    "$ARDUINO_CLI" lib search "$keyword"
}

add_library() {
    local library="$1"
    if [ -z "$library" ]; then
        echo -e "${RED}Error: Please provide a library name${NC}"
        exit 1
    fi
    
    # Check if library is already in the file
    if grep -Fxq "$library" "$LIBRARIES_FILE" 2>/dev/null; then
        echo -e "${YELLOW}Library '$library' is already in $LIBRARIES_FILE${NC}"
        return
    fi
    
    # Add library to file
    echo "$library" >> "$LIBRARIES_FILE"
    echo -e "${GREEN}Added '$library' to $LIBRARIES_FILE${NC}"
    
    # Install the library
    check_arduino_cli
    echo -e "${CYAN}Installing library...${NC}"
    "$ARDUINO_CLI" lib install "$library"
    echo -e "${GREEN}Library installed successfully${NC}"
}

remove_library() {
    local library="$1"
    if [ -z "$library" ]; then
        echo -e "${RED}Error: Please provide a library name${NC}"
        exit 1
    fi
    
    if [ ! -f "$LIBRARIES_FILE" ]; then
        echo -e "${RED}Error: $LIBRARIES_FILE not found${NC}"
        exit 1
    fi
    
    # Remove library from file
    if grep -Fxq "$library" "$LIBRARIES_FILE"; then
        # Create temp file without the library
        grep -Fxv "$library" "$LIBRARIES_FILE" > "$LIBRARIES_FILE.tmp"
        mv "$LIBRARIES_FILE.tmp" "$LIBRARIES_FILE"
        echo -e "${GREEN}Removed '$library' from $LIBRARIES_FILE${NC}"
        echo -e "${YELLOW}Note: Library is not uninstalled, only removed from config${NC}"
    else
        echo -e "${YELLOW}Library '$library' not found in $LIBRARIES_FILE${NC}"
    fi
}

list_libraries() {
    if [ ! -f "$LIBRARIES_FILE" ]; then
        echo -e "${YELLOW}No libraries configured (${LIBRARIES_FILE} not found)${NC}"
        return
    fi
    
    echo -e "${CYAN}Configured libraries in $LIBRARIES_FILE:${NC}"
    echo ""
    
    local count=0
    while IFS= read -r line || [ -n "$line" ]; do
        # Skip empty lines and comments
        if [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]]; then
            continue
        fi
        
        library=$(echo "$line" | xargs)
        if [ -n "$library" ]; then
            echo -e "  ${GREEN}✓${NC} $library"
            count=$((count + 1))
        fi
    done < "$LIBRARIES_FILE"
    
    if [ $count -eq 0 ]; then
        echo -e "  ${YELLOW}(no libraries configured yet)${NC}"
    fi
    
    echo ""
    echo -e "${BLUE}Total: $count libraries${NC}"
}

install_all() {
    if [ ! -f "$LIBRARIES_FILE" ]; then
        echo -e "${RED}Error: $LIBRARIES_FILE not found${NC}"
        exit 1
    fi
    
    check_arduino_cli
    echo -e "${CYAN}Installing all libraries from $LIBRARIES_FILE...${NC}"
    echo ""
    
    while IFS= read -r line || [ -n "$line" ]; do
        # Skip empty lines and comments
        if [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]]; then
            continue
        fi
        
        library=$(echo "$line" | xargs)
        if [ -n "$library" ]; then
            echo -e "${CYAN}Installing: $library${NC}"
            "$ARDUINO_CLI" lib install "$library" || echo -e "${YELLOW}Warning: Failed to install $library${NC}"
        fi
    done < "$LIBRARIES_FILE"
    
    echo ""
    echo -e "${GREEN}Installation complete${NC}"
}

show_installed() {
    check_arduino_cli
    echo -e "${CYAN}Currently installed libraries:${NC}"
    "$ARDUINO_CLI" lib list
}

# Main command dispatcher
case "${1:-}" in
    search)
        search_library "$2"
        ;;
    add)
        add_library "$2"
        ;;
    remove)
        remove_library "$2"
        ;;
    list)
        list_libraries
        ;;
    install)
        install_all
        ;;
    installed)
        show_installed
        ;;
    *)
        show_usage
        exit 1
        ;;
esac
