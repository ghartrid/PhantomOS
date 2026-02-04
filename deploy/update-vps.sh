#!/bin/bash
# PhantomOS VPS Update Script
# Uploads latest code and rebuilds on VPS

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Configuration from environment or prompt
VPS_USER="${PHANTOMOS_VPS_USER:-root}"
VPS_IP="${PHANTOMOS_VPS_IP:-}"
LOCAL_TARBALL="/home/graham/Desktop/phantomos.tar.gz"
LOCAL_PHANTOMOS="/home/graham/Desktop/phantomos"

# Prompt for VPS IP if not set
if [ -z "$VPS_IP" ]; then
    read -p "Enter VPS IP address: " VPS_IP
    if [ -z "$VPS_IP" ]; then
        echo -e "${RED}VPS IP is required${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  PhantomOS VPS Update Script${NC}"
echo -e "${GREEN}========================================${NC}"

# Step 1: Create fresh tarball
echo -e "${YELLOW}[1/5] Creating tarball...${NC}"
cd /home/graham/Desktop
tar -czvf phantomos.tar.gz phantomos/ > /dev/null
echo -e "${GREEN}Tarball created: $(ls -lh $LOCAL_TARBALL | awk '{print $5}')${NC}"

# Step 2: Upload to VPS
echo -e "${YELLOW}[2/5] Uploading to VPS...${NC}"
scp "$LOCAL_TARBALL" "${VPS_USER}@${VPS_IP}:~/"
echo -e "${GREEN}Upload complete${NC}"

# Step 3: Extract on VPS
echo -e "${YELLOW}[3/5] Extracting on VPS...${NC}"
ssh "${VPS_USER}@${VPS_IP}" "rm -rf ~/phantomos && tar -xzf ~/phantomos.tar.gz -C ~/"
echo -e "${GREEN}Extraction complete${NC}"

# Step 4: Rebuild on VPS
echo -e "${YELLOW}[4/5] Rebuilding on VPS...${NC}"
ssh "${VPS_USER}@${VPS_IP}" "cd ~/phantomos/kernel && make clean && make"
echo -e "${GREEN}Build complete${NC}"

# Step 5: Stop entire stack, deploy, and restart
echo -e "${YELLOW}[5/5] Stopping stack, deploying, and restarting...${NC}"

ssh "${VPS_USER}@${VPS_IP}" << 'EOF'
# Stop entire display stack
supervisorctl stop phantomos-stack:*
sleep 2

# Clear old user data
rm -f /opt/phantomos/*.geo /opt/phantomos/phantom_users.dat

# Deploy new binary
cp ~/phantomos/kernel/phantom-gui /opt/phantomos/
chown phantomos:phantomos /opt/phantomos/phantom-gui
chmod +x /opt/phantomos/phantom-gui

# Restart entire stack (xvfb -> x11vnc -> websockify -> phantomos)
supervisorctl start phantomos-stack:*
sleep 3
supervisorctl status
EOF

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Update Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
