#!/bin/bash
echo Setting the date to $(date)

function set_sudo() {
    if [[ $(whoami) == "root" ]]; then
        SUDO=""
    else
        SUDO="sudo "
    fi
}
function check_nvme_cli() {
    if (! [[ -x "$(command -v nvme)" ]]) && (! [[ -x "$(${SUDO}which nvme)" ]]); then
        echo "Error: NVMe-CLI is not installed. Please install it and try again!" >&2
        exit 1
    fi
    return 0
}

set_sudo
check_nvme_cli
# Current epoch time in milliseconds
epoch=$(date +%s%3N)
# Convert to hexadecimal
epoch_hex=$(printf "%016x" $epoch)
# Reverse the byte order (convert to little endian)
epoch_le=$(echo "$epoch_hex" | sed 's/\(..\)/\1 /g' | tac -s ' ' | tr -d ' ')
# Convert to an escaped string
escaped_string=$(echo "$epoch_le" | sed 's/\(..\)/\\x\1/g')
# Send nvme command to set the date and time
# Get nvme-cli version
NVME_VERSION=$(${SUDO} nvme --version | grep -oP '^nvme version \K\d+\.\d+(\.\d+)?')

# Parse device nodes for Quadra cards based on nvme-cli version
if [[ $(printf "%s\n%s" "$NVME_VERSION" "2.0" | sort -V | head -n 1) == "2.0" ]]; then
    # For nvme-cli >= 2.0
    XCOD_DATA=($(${SUDO} nvme list | sed -e '1,2d' | grep -P "Quadra" | awk '{print $1}'))
else
    # For nvme-cli < 2.0
    XCOD_DATA=($(${SUDO} nvme list | sed -e '1,2d' | grep -P "Quadra" | awk '{print $1}'))
fi

# Loop through all detected Quadra devices and set the timestamp
for NVME_NODE in "${XCOD_DATA[@]}"; do
    CONTROLLER_NODE=$(echo "$NVME_NODE" | sed 's/n[0-9]\+$//')
    echo "Setting timestamp on $CONTROLLER_NODE"
    echo -n -e ${escaped_string} | ${SUDO} nvme set-feature "$CONTROLLER_NODE" --feature-id=0x0e --data-len=8 > /dev/null
done

