#!/bin/bash
# PhantomOS VPS Setup Script
# Run via SSH: ssh root@YOUR_VPS_IP 'bash -s' < vps-setup.sh
# Or: scp vps-setup.sh root@YOUR_VPS_IP: && ssh root@YOUR_VPS_IP bash vps-setup.sh
#
# Prerequisites: phantomos.iso must be uploaded to /root/phantomos.iso
# Upload with: scp phantomos.iso root@YOUR_VPS_IP:/root/phantomos.iso

set -e

DOMAIN="demo.phantomos.net"
ISO_PATH="/root/phantomos.iso"
NOVNC_PATH="/usr/share/novnc"

echo "=== PhantomOS VPS Setup ==="

# Check ISO exists
if [ ! -f "$ISO_PATH" ]; then
    echo "ERROR: $ISO_PATH not found!"
    echo "Upload it first: scp phantomos.iso root@YOUR_VPS_IP:/root/phantomos.iso"
    exit 1
fi

# --- 1. Install packages ---
echo "=== Installing packages ==="
apt update
apt install -y qemu-system-x86 novnc python3-websockify nginx certbot python3-certbot-nginx ufw fail2ban

# --- 2. Configure UFW firewall ---
echo "=== Configuring firewall ==="
ufw default deny incoming
ufw default allow outgoing
ufw allow ssh
ufw allow 80/tcp
ufw allow 443/tcp
ufw --force enable

# --- 3. Configure fail2ban ---
echo "=== Configuring fail2ban ==="
cat > /etc/fail2ban/jail.local << 'EOF'
[DEFAULT]
bantime = 1h
findtime = 10m
maxretry = 5
banaction = ufw

[sshd]
enabled = true
port = ssh
maxretry = 3
bantime = 24h

[nginx-http-auth]
enabled = true
port = http,https

[nginx-botsearch]
enabled = true
port = http,https

[nginx-limit-req]
enabled = true
port = http,https
EOF

systemctl enable fail2ban
systemctl restart fail2ban

# --- 4. Create QEMU systemd service ---
echo "=== Creating QEMU service ==="
cat > /etc/systemd/system/phantomos.service << 'EOF'
[Unit]
Description=PhantomOS QEMU Instance
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/qemu-system-x86_64 -cdrom /root/phantomos.iso -m 512 -vga std -vnc :0 -display none -audiodev none,id=noaudio -usbdevice tablet
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# --- 5. Create websockify systemd service ---
echo "=== Creating websockify service ==="
cat > /etc/systemd/system/websockify.service << 'EOF'
[Unit]
Description=Websockify for PhantomOS noVNC
After=phantomos.service
Requires=phantomos.service

[Service]
Type=simple
ExecStart=/usr/bin/websockify --web /usr/share/novnc 6080 localhost:5900
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# --- 6. Create nginx config ---
echo "=== Configuring nginx ==="
cat > /etc/nginx/sites-available/phantomos << NGINXEOF
server {
    listen 80;
    server_name ${DOMAIN};

    # Redirect root to noVNC client
    location = / {
        return 301 /vnc.html?autoconnect=true&resize=scale;
    }

    location / {
        proxy_pass http://localhost:6080;
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
NGINXEOF

# Enable site, disable default
ln -sf /etc/nginx/sites-available/phantomos /etc/nginx/sites-enabled/phantomos
rm -f /etc/nginx/sites-enabled/default

# Test and reload nginx
nginx -t
systemctl restart nginx

# --- 7. Kill any stale processes ---
echo "=== Cleaning up stale processes ==="
fuser -k 5900/tcp 2>/dev/null || true
fuser -k 6080/tcp 2>/dev/null || true
sleep 1

# --- 8. Enable and start services ---
echo "=== Starting services ==="
systemctl daemon-reload
systemctl enable phantomos.service
systemctl enable websockify.service
systemctl start phantomos.service
sleep 2
systemctl start websockify.service

# --- 9. Verify services ---
echo "=== Verifying ==="
systemctl status phantomos.service --no-pager -l
echo ""
systemctl status websockify.service --no-pager -l

# --- 10. SSL certificate ---
echo ""
echo "=== Setting up SSL ==="
certbot --nginx -d "$DOMAIN" --non-interactive --agree-tos --register-unsafely-without-email --redirect

# Re-apply redirect after certbot modifies nginx config
# (certbot may overwrite the location = / block)
echo "=== Re-applying root redirect ==="
cat > /etc/nginx/sites-available/phantomos << NGINXEOF2
server {
    listen 80;
    server_name ${DOMAIN};
    return 301 https://\$server_name\$request_uri;
}

server {
    listen 443 ssl http2;
    server_name ${DOMAIN};

    ssl_certificate /etc/letsencrypt/live/${DOMAIN}/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/${DOMAIN}/privkey.pem;
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

    # Redirect root to noVNC client
    location = / {
        return 301 /vnc.html?autoconnect=true&resize=scale;
    }

    location / {
        proxy_pass http://localhost:6080;
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
NGINXEOF2
nginx -t && systemctl reload nginx

echo ""
echo "=== DONE ==="
echo "PhantomOS is running at: https://${DOMAIN}/vnc.html?autoconnect=true&resize=scale"
echo ""
echo "Services auto-start on reboot."
echo "  Check status:  systemctl status phantomos websockify"
echo "  View logs:     journalctl -u phantomos -f"
echo "  Restart:       systemctl restart phantomos websockify"
echo ""
echo "Security:"
echo "  Firewall:      ufw status"
echo "  Banned IPs:    fail2ban-client status sshd"
echo "  Unban IP:      fail2ban-client set sshd unbanip IP_ADDRESS"
