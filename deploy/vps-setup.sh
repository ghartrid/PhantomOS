#!/bin/bash
# PhantomOS VPS Deployment Script
# For Ubuntu 22.04/24.04 on Namecheap Quasar VPS
# Sets up PhantomOS with noVNC accessible via domain

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  PhantomOS VPS Deployment Script${NC}"
echo -e "${GREEN}========================================${NC}"

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Please run as root (sudo ./vps-setup.sh)${NC}"
    exit 1
fi

# Get domain from user
read -p "Enter your domain (e.g., phantomos.example.com): " DOMAIN
read -p "Enter your email (for SSL certificate): " EMAIL

# Secure password input (hidden from terminal and process list)
echo -n "Set a VNC password (for security): "
read -s VNC_PASSWORD
echo ""
echo -n "Confirm VNC password: "
read -s VNC_PASSWORD_CONFIRM
echo ""

if [ -z "$DOMAIN" ] || [ -z "$EMAIL" ] || [ -z "$VNC_PASSWORD" ]; then
    echo -e "${RED}All fields are required${NC}"
    exit 1
fi

if [ "$VNC_PASSWORD" != "$VNC_PASSWORD_CONFIRM" ]; then
    echo -e "${RED}Passwords do not match${NC}"
    exit 1
fi

# Validate password strength
if [ ${#VNC_PASSWORD} -lt 8 ]; then
    echo -e "${RED}Password must be at least 8 characters${NC}"
    exit 1
fi

echo -e "${YELLOW}Updating system...${NC}"
apt update && apt upgrade -y

echo -e "${YELLOW}Installing dependencies...${NC}"

# Detect Ubuntu version and set WebKit package name
UBUNTU_VERSION=$(lsb_release -rs)
if dpkg --compare-versions "$UBUNTU_VERSION" "ge" "24.04"; then
    WEBKIT_PKG="libwebkit2gtk-4.1-dev"
else
    WEBKIT_PKG="libwebkit2gtk-4.0-dev"
fi

apt install -y \
    build-essential \
    libgtk-3-dev \
    $WEBKIT_PKG \
    libcurl4-openssl-dev \
    libssl-dev \
    libavahi-client-dev \
    xvfb \
    x11vnc \
    novnc \
    websockify \
    nginx \
    certbot \
    python3-certbot-nginx \
    supervisor \
    git \
    dbus-x11 \
    at-spi2-core \
    libcanberra-gtk3-module \
    packagekit-gtk3-module \
    fonts-dejavu \
    fonts-liberation \
    fail2ban

echo -e "${YELLOW}Creating PhantomOS user...${NC}"
useradd -m -s /bin/bash phantomos || true

# Set password securely using stdin redirect (avoids process list exposure)
echo "$VNC_PASSWORD" | passwd --stdin phantomos 2>/dev/null || \
    chpasswd <<< "phantomos:$VNC_PASSWORD"

# Clear password from memory
VNC_PASSWORD_CONFIRM=""

echo -e "${YELLOW}Creating deploy user (for secure deployments)...${NC}"
useradd -m -s /bin/bash deploy || true

# Set up SSH key authentication for deploy user (more secure than password)
mkdir -p /home/deploy/.ssh
chmod 700 /home/deploy/.ssh
touch /home/deploy/.ssh/authorized_keys
chmod 600 /home/deploy/.ssh/authorized_keys
chown -R deploy:deploy /home/deploy/.ssh

# Grant deploy user limited sudo for deployment tasks only
cat > /etc/sudoers.d/phantomos-deploy << 'SUDOERS'
# PhantomOS deploy user - limited privileges
deploy ALL=(phantomos) NOPASSWD: /opt/phantomos/phantom-gui
deploy ALL=(root) NOPASSWD: /usr/bin/supervisorctl restart phantomos-stack\:*
deploy ALL=(root) NOPASSWD: /usr/bin/supervisorctl start phantomos-stack\:*
deploy ALL=(root) NOPASSWD: /usr/bin/supervisorctl stop phantomos-stack\:*
deploy ALL=(root) NOPASSWD: /usr/bin/supervisorctl status
deploy ALL=(root) NOPASSWD: /bin/cp /home/deploy/phantomos/kernel/phantom-gui /opt/phantomos/
deploy ALL=(root) NOPASSWD: /bin/chown phantomos\:phantomos /opt/phantomos/phantom-gui
deploy ALL=(root) NOPASSWD: /bin/chmod +x /opt/phantomos/phantom-gui
SUDOERS
chmod 440 /etc/sudoers.d/phantomos-deploy

echo -e "${GREEN}Deploy user created. Add your SSH public key to:${NC}"
echo -e "  /home/deploy/.ssh/authorized_keys"
echo -e "${YELLOW}Then update your deploy script to use 'deploy' user instead of 'root'${NC}"

echo -e "${YELLOW}Setting up PhantomOS directory...${NC}"
mkdir -p /opt/phantomos
chown phantomos:phantomos /opt/phantomos
mkdir -p /home/phantomos/.vnc

# Copy PhantomOS files (assumes script is run from deploy directory)
if [ -d "../kernel" ]; then
    echo -e "${YELLOW}Building PhantomOS...${NC}"
    cd ../kernel
    make clean && make
    cp phantom-gui /opt/phantomos/
    cp face_track.py /opt/phantomos/ 2>/dev/null || true
    cp voice_recognize.py /opt/phantomos/ 2>/dev/null || true
    chown -R phantomos:phantomos /opt/phantomos
    cd ../deploy
else
    echo -e "${YELLOW}PhantomOS kernel directory not found.${NC}"
    echo -e "${YELLOW}Please copy phantom-gui to /opt/phantomos/ manually${NC}"
fi

# Set up VNC password
echo -e "${YELLOW}Configuring VNC...${NC}"
x11vnc -storepasswd "$VNC_PASSWORD" /home/phantomos/.vnc/passwd
chown -R phantomos:phantomos /home/phantomos/.vnc
chmod 600 /home/phantomos/.vnc/passwd

# Clear password from memory/environment for security
unset VNC_PASSWORD
unset VNC_PASSWORD_CONFIRM

echo -e "${YELLOW}Creating Xvfb startup script...${NC}"
cat > /opt/phantomos/start-display.sh << 'EOF'
#!/bin/bash
export DISPLAY=:99
Xvfb :99 -screen 0 1920x1080x24 &
sleep 2
dbus-launch --exit-with-session /opt/phantomos/phantom-gui &
EOF
chmod +x /opt/phantomos/start-display.sh

echo -e "${YELLOW}Configuring Supervisor...${NC}"
cat > /etc/supervisor/conf.d/phantomos.conf << EOF
[program:xvfb]
command=/usr/bin/Xvfb :99 -screen 0 1920x1080x24
user=phantomos
autostart=true
autorestart=true
stdout_logfile=/var/log/phantomos/xvfb.log
stderr_logfile=/var/log/phantomos/xvfb.err
priority=100

[program:phantomos]
command=/opt/phantomos/phantom-gui
directory=/opt/phantomos
user=phantomos
environment=DISPLAY=":99",HOME="/home/phantomos",DBUS_SESSION_BUS_ADDRESS="autolaunch:"
autostart=true
autorestart=true
stdout_logfile=/var/log/phantomos/phantomos.log
stderr_logfile=/var/log/phantomos/phantomos.err
priority=200
startsecs=5

[program:x11vnc]
command=/usr/bin/x11vnc -display :99 -forever -shared -rfbauth /home/phantomos/.vnc/passwd -rfbport 5900
user=phantomos
autostart=true
autorestart=true
stdout_logfile=/var/log/phantomos/x11vnc.log
stderr_logfile=/var/log/phantomos/x11vnc.err
priority=300

[program:websockify]
command=/usr/bin/websockify --web=/usr/share/novnc 6080 localhost:5900
user=phantomos
autostart=true
autorestart=true
stdout_logfile=/var/log/phantomos/websockify.log
stderr_logfile=/var/log/phantomos/websockify.err
priority=400

[group:phantomos-stack]
programs=xvfb,phantomos,x11vnc,websockify
EOF

mkdir -p /var/log/phantomos
chown phantomos:phantomos /var/log/phantomos

echo -e "${YELLOW}Configuring Nginx (HTTP first for certbot)...${NC}"
cat > /etc/nginx/sites-available/phantomos << EOF
server {
    listen 80;
    server_name $DOMAIN;

    # Proxy to noVNC (will be upgraded to HTTPS by certbot)
    location / {
        proxy_pass http://127.0.0.1:6080/;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 86400;
    }
}
EOF

ln -sf /etc/nginx/sites-available/phantomos /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default

# Test nginx config
nginx -t

echo -e "${YELLOW}Setting up firewall...${NC}"
ufw allow 22/tcp
ufw allow 80/tcp
ufw allow 443/tcp
ufw --force enable

echo -e "${YELLOW}Configuring fail2ban for SSH protection...${NC}"
cat > /etc/fail2ban/jail.local << 'EOF'
[DEFAULT]
bantime = 3600
findtime = 600
maxretry = 5
banaction = ufw

[sshd]
enabled = true
port = ssh
filter = sshd
logpath = /var/log/auth.log
maxretry = 3
bantime = 7200

[sshd-ddos]
enabled = true
port = ssh
filter = sshd-ddos
logpath = /var/log/auth.log
maxretry = 6
bantime = 3600
EOF

systemctl enable fail2ban
systemctl restart fail2ban

echo -e "${YELLOW}Hardening SSH configuration...${NC}"
# Check if SSH is installed
if [ -f /etc/ssh/sshd_config ]; then
    # Backup original sshd_config
    cp /etc/ssh/sshd_config /etc/ssh/sshd_config.backup
    mkdir -p /etc/ssh/sshd_config.d

    # Create hardened SSH config
    cat > /etc/ssh/sshd_config.d/phantomos-hardening.conf << 'EOF'
# PhantomOS SSH Hardening
# Disable root login (use deploy user with sudo instead)
PermitRootLogin no

# Disable password authentication (use SSH keys)
# Uncomment after adding your SSH key to deploy user:
# PasswordAuthentication no

# Limit login attempts
MaxAuthTries 3
LoginGraceTime 60

# Disable empty passwords
PermitEmptyPasswords no

# Only allow specific users
AllowUsers deploy phantomos

# Disable X11 forwarding (not needed for VNC setup)
X11Forwarding no
EOF

    # Test SSH config before restarting
    if sshd -t; then
        systemctl restart sshd
        echo -e "${GREEN}SSH hardening applied${NC}"
    else
        echo -e "${RED}SSH config error - reverting${NC}"
        rm /etc/ssh/sshd_config.d/phantomos-hardening.conf
    fi
else
    echo -e "${YELLOW}SSH not installed - skipping hardening${NC}"
fi

echo -e "${YELLOW}Starting services...${NC}"
systemctl restart nginx
supervisorctl reread
supervisorctl update
supervisorctl start phantomos-stack:*

echo -e "${YELLOW}Obtaining SSL certificate...${NC}"
certbot --nginx -d "$DOMAIN" --email "$EMAIL" --agree-tos --non-interactive --redirect

# Update nginx config with WebSocket support after certbot
echo -e "${YELLOW}Updating Nginx for WebSocket support...${NC}"
cat > /etc/nginx/sites-available/phantomos << EOF
server {
    listen 80;
    server_name $DOMAIN;
    return 301 https://\$server_name\$request_uri;
}

server {
    listen 443 ssl http2;
    server_name $DOMAIN;

    ssl_certificate /etc/letsencrypt/live/$DOMAIN/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/$DOMAIN/privkey.pem;
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

    # Redirect root to noVNC client
    location = / {
        return 301 /vnc.html?autoconnect=true&resize=scale;
    }

    # noVNC web interface
    location / {
        proxy_pass http://127.0.0.1:6080/;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 86400;
    }
}
EOF

nginx -t && systemctl reload nginx

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  PhantomOS Deployment Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "Access PhantomOS at: ${GREEN}https://$DOMAIN${NC}"
echo -e "VNC Password: ${YELLOW}$VNC_PASSWORD${NC}"
echo ""
echo -e "Useful commands:"
echo -e "  ${YELLOW}supervisorctl status${NC}          - Check service status"
echo -e "  ${YELLOW}supervisorctl restart all${NC}     - Restart all services"
echo -e "  ${YELLOW}tail -f /var/log/phantomos/*.log${NC} - View logs"
echo ""
echo -e "${YELLOW}Note: Make sure your domain's DNS A record points to this server's IP${NC}"
