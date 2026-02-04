#!/bin/bash
# PhantomOS Complete VPS Setup Script
# Run this on a fresh VPS

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  PhantomOS Complete VPS Setup${NC}"
echo -e "${GREEN}========================================${NC}"

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Run as root${NC}"
    exit 1
fi

# Get inputs
read -p "Domain (e.g. phantomos.example.com): " DOMAIN
read -p "Email (for SSL): " EMAIL
read -s -p "VNC Password: " VNC_PASS
echo ""

echo -e "${YELLOW}[1/8] Installing packages...${NC}"
apt update
apt install -y build-essential gcc make pkg-config \
    libgtk-3-dev libwebkit2gtk-4.1-dev libssl-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    xvfb x11vnc novnc websockify nginx supervisor \
    certbot python3-certbot-nginx dbus-x11

echo -e "${YELLOW}[2/8] Creating user...${NC}"
useradd -m -s /bin/bash phantomos 2>/dev/null || true

echo -e "${YELLOW}[3/8] Creating directories...${NC}"
mkdir -p /opt/phantomos
mkdir -p /home/phantomos/.vnc
mkdir -p /var/log/phantomos
chown -R phantomos:phantomos /opt/phantomos /home/phantomos /var/log/phantomos

echo -e "${YELLOW}[4/8] Setting VNC password...${NC}"
x11vnc -storepasswd "$VNC_PASS" /home/phantomos/.vnc/passwd
chown phantomos:phantomos /home/phantomos/.vnc/passwd
chmod 600 /home/phantomos/.vnc/passwd

echo -e "${YELLOW}[5/8] Building PhantomOS...${NC}"
if [ -d ~/phantomos/kernel ]; then
    cd ~/phantomos/kernel
    make clean && make
    cp phantom-gui /opt/phantomos/
    chown phantomos:phantomos /opt/phantomos/phantom-gui
    chmod +x /opt/phantomos/phantom-gui
fi

echo -e "${YELLOW}[6/8] Creating supervisor config...${NC}"
cat > /etc/supervisor/conf.d/phantomos.conf << 'SUPEOF'
[program:xvfb]
command=/usr/bin/Xvfb :99 -screen 0 1920x1080x24
user=phantomos
autostart=true
autorestart=true
priority=100

[program:phantomos]
command=/opt/phantomos/phantom-gui
directory=/opt/phantomos
user=phantomos
environment=DISPLAY=":99",HOME="/home/phantomos"
autostart=true
autorestart=true
priority=200

[program:x11vnc]
command=/usr/bin/x11vnc -display :99 -forever -shared -rfbauth /home/phantomos/.vnc/passwd -rfbport 5900
user=phantomos
autostart=true
autorestart=true
priority=300

[program:websockify]
command=/usr/bin/websockify --web=/usr/share/novnc 6080 localhost:5900
user=phantomos
autostart=true
autorestart=true
priority=400

[group:phantomos-stack]
programs=xvfb,phantomos,x11vnc,websockify
SUPEOF

echo -e "${YELLOW}[7/8] Creating nginx config...${NC}"
cat > /etc/nginx/sites-available/phantomos << NGEOF
server {
    listen 80;
    server_name $DOMAIN;
    location / {
        proxy_pass http://127.0.0.1:6080/;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_read_timeout 86400;
    }
}
NGEOF

ln -sf /etc/nginx/sites-available/phantomos /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default

echo -e "${YELLOW}[8/8] Starting services...${NC}"
systemctl enable supervisor
systemctl restart supervisor
systemctl restart nginx
sleep 2
supervisorctl reread
supervisorctl update
supervisorctl start phantomos-stack:*

echo -e "${YELLOW}Getting SSL certificate...${NC}"
certbot --nginx -d "$DOMAIN" --email "$EMAIL" --agree-tos --non-interactive --redirect || true

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Setup Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "Access: ${GREEN}https://$DOMAIN${NC}"
echo ""
supervisorctl status
